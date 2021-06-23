/*
Copyright (c) 2018 Raspberry Pi (Trading) Ltd.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

#include <X11/Xlib.h>

#include <libintl.h>

/* Controls */

static GtkWidget *msg_dlg, *msg_msg, *msg_pb, *msg_btn, *msg_pbv;

gchar *pnames[2];
gboolean needs_reboot;
int calls;

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static gboolean net_available (void);
static gboolean clock_synced (void);
static PkResults *error_handler (PkTask *task, GAsyncResult *res, char *desc);
static void message (char *msg, int prog);
static gboolean quit (GtkButton *button, gpointer data);
static gboolean refresh_cache (gpointer data);
static void start_install (PkTask *task, GAsyncResult *res, gpointer data);
static void resolve_done (PkTask *task, GAsyncResult *res, gpointer data);
static void install_done (PkTask *task, GAsyncResult *res, gpointer data);
static gboolean close_end (gpointer data);
static void progress (PkProgress *progress, PkProgressType *type, gpointer data);


/*----------------------------------------------------------------------------*/
/* Helper functions for system status                                         */
/*----------------------------------------------------------------------------*/

static gboolean net_available (void)
{
    if (system ("hostname -I | grep -q \\\\.") == 0) return TRUE;
    else return FALSE;
}

static gboolean clock_synced (void)
{
    if (system ("test -e /usr/sbin/ntpd") == 0)
    {
        if (system ("ntpq -p | grep -q ^\\*") == 0) return TRUE;
    }
    else
    {
        if (system ("timedatectl status | grep -q \"synchronized: yes\"") == 0) return TRUE;
    }
    return FALSE;
}


/*----------------------------------------------------------------------------*/
/* Helper functions for async operations                                      */
/*----------------------------------------------------------------------------*/

static PkResults *error_handler (PkTask *task, GAsyncResult *res, char *desc)
{
    PkResults *results;
    PkError *pkerror;
    GError *error = NULL;
    gchar *buf;

    results = pk_task_generic_finish (task, res, &error);
    if (error != NULL)
    {
        buf = g_strdup_printf (_("Error %s - %s"), desc, error->message);
        message (buf, -3);
        g_free (buf);
        return NULL;
    }

    pkerror = pk_results_get_error_code (results);
    if (pkerror != NULL)
    {
        buf = g_strdup_printf (_("Error %s - %s"), desc, pk_error_get_details (pkerror));
        message (buf, -3);
        g_free (buf);
        return NULL;
    }

    return results;
}


/*----------------------------------------------------------------------------*/
/* Progress / error box                                                       */
/*----------------------------------------------------------------------------*/

static void message (char *msg, int prog)
{
    if (!msg_dlg)
    {
        GtkBuilder *builder;

        builder = gtk_builder_new ();
        printf ("%d\n", gtk_builder_add_from_file (builder, PACKAGE_DATA_DIR "/ui/lxplug-updater.ui", NULL));

        msg_dlg = (GtkWidget *) gtk_builder_get_object (builder, "modal");

        msg_msg = (GtkWidget *) gtk_builder_get_object (builder, "modal_msg");
        msg_pb = (GtkWidget *) gtk_builder_get_object (builder, "modal_pb");
        msg_btn = (GtkWidget *) gtk_builder_get_object (builder, "modal_ok");

        gtk_label_set_text (GTK_LABEL (msg_msg), msg);

        gtk_widget_show_all (msg_dlg);
        g_object_unref (builder);
    }
    else gtk_label_set_text (GTK_LABEL (msg_msg), msg);

    gtk_widget_set_visible (msg_btn, prog == -3);
    gtk_widget_set_visible (msg_pb, prog > -2);
    g_signal_connect (msg_btn, "clicked", G_CALLBACK (quit), NULL);

    if (prog >= 0)
    {
        float progress = prog / 100.0;
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (msg_pb), progress);
    }
    else if (prog == -1) gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
}

static gboolean quit (GtkButton *button, gpointer data)
{
    if (msg_dlg)
    {
        gtk_widget_destroy (GTK_WIDGET (msg_dlg));
        msg_dlg = NULL;
    }

    gtk_main_quit ();
    return FALSE;
}


/*----------------------------------------------------------------------------*/
/* Handlers for asynchronous install sequence                                 */
/*----------------------------------------------------------------------------*/

static gboolean refresh_cache (gpointer data)
{
    PkTask *task;

    message (_("Updating package data - please wait..."), -1);

    task = pk_task_new ();

    pk_client_refresh_cache_async (PK_CLIENT (task), TRUE, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) start_install, NULL);
    return FALSE;
}

static void start_install (PkTask *task, GAsyncResult *res, gpointer data)
{
    if (!error_handler (task, res, _("updating cache"))) return;

    message (_("Installing - please wait..."), -1);

    pk_client_resolve_async (PK_CLIENT (task), 0, pnames, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) resolve_done, NULL);
}

static void resolve_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    PkResults *results;
    PkPackageSack *sack;
    GPtrArray *array;
    PkPackage *item;
    PkInfoEnum info;
    gchar *package_id;
    gchar *pinst[2];

    results = error_handler (task, res, _("finding packages"));
    if (!results) return;

    sack = pk_results_get_package_sack (results);
    array = pk_package_sack_get_array (sack);
    if (array->len == 0)
    {
        message (_("Package not found - exiting"), -3);
    }
    else
    {
        item = g_ptr_array_index (array, 0);
        g_object_get (item, "info", &info, "package-id", &package_id, NULL);

        if (info == PK_INFO_ENUM_INSTALLED)
        {
            message (_("Already installed - exiting"), -3);
        }
        else
        {
            pinst[0] = package_id;
            pinst[1] = NULL;

            pk_task_install_packages_async (task, pinst, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) install_done, NULL);
        }
    }

    g_ptr_array_unref (array);
    g_object_unref (sack);
}

static void install_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    if (!error_handler (task, res, _("installing packages"))) return;

    if (needs_reboot)
    {
        message (_("Installation complete - rebooting"), -2);
    }
    else
    {
        message (_("Installation complete"), -2);
    }

    g_timeout_add_seconds (2, close_end, NULL);
}

static gboolean close_end (gpointer data)
{
    if (msg_dlg)
    {
        gtk_widget_destroy (GTK_WIDGET (msg_dlg));
        msg_dlg = NULL;
    }

    if (needs_reboot) system ("reboot");

    gtk_main_quit ();
    return FALSE;
}

static void progress (PkProgress *progress, PkProgressType *type, gpointer data)
{
    char *buf, *name;
    int role = pk_progress_get_role (progress);
    int status = pk_progress_get_status (progress);

    if (msg_dlg)
    {
        switch (role)
        {
            case PK_ROLE_ENUM_REFRESH_CACHE :       if (status == PK_STATUS_ENUM_LOADING_CACHE)
                                                        message (_("Updating package data - please wait..."), pk_progress_get_percentage (progress));
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;

            case PK_ROLE_ENUM_RESOLVE :             if (status == PK_STATUS_ENUM_LOADING_CACHE)
                                                        message (_("Finding package - please wait..."), pk_progress_get_percentage (progress));
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;

            case PK_ROLE_ENUM_UPDATE_PACKAGES :     if (status == PK_STATUS_ENUM_LOADING_CACHE)
                                                        message (_("Updating application - please wait..."), pk_progress_get_percentage (progress));
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;

            case PK_ROLE_ENUM_GET_DETAILS :         if (status == PK_STATUS_ENUM_LOADING_CACHE)
                                                        message (_("Reading package details - please wait..."), pk_progress_get_percentage (progress));
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;

            case PK_ROLE_ENUM_INSTALL_PACKAGES :    if (status == PK_STATUS_ENUM_DOWNLOAD || status == PK_STATUS_ENUM_INSTALL)
                                                    {
                                                        buf = g_strdup_printf (_("%s package - please wait..."), status == PK_STATUS_ENUM_INSTALL ? _("Installing") : _("Downloading"));
                                                        message (buf, pk_progress_get_percentage (progress));
                                                    }
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;
        }
    }
}


/*----------------------------------------------------------------------------*/
/* Main window                                                                */
/*----------------------------------------------------------------------------*/

int main (int argc, char *argv[])
{
    char *buf;
    int res;

    // check a package name was supplied
    if (argc < 2)
    {
        printf ("No package name specified\n");
        return -1;
    }

    // check the supplied package exists and is not already installed 
    buf = g_strdup_printf ("apt-cache policy %s | grep -q \"Installed: (none)\"", argv[1]);
    res = system (buf);
    g_free (buf);
    if (res != 0)
    {
        printf ("Package not found or already installed\n");
        return -1;
    }

    // check the network is connected
    if (!net_available ())
    {
        printf ("No network connection\n");
        return -1;
    }

    // check the clock is synced (as otherwise apt is unhappy)
    if (!clock_synced ())
    {
        printf ("Clock not synchronised - try again later\n");
        return -1;
    }

#ifdef ENABLE_NLS
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
#endif

    // GTK setup
    gtk_init (&argc, &argv);
    gtk_icon_theme_prepend_search_path (gtk_icon_theme_get_default(), PACKAGE_DATA_DIR);

    // create a package name array using the command line argument
    pnames[0] = argv[1];
    pnames[1] = NULL;

    if (argc > 2 && !g_strcmp0 (argv[2], "reboot")) needs_reboot = TRUE;
    else needs_reboot = FALSE;

    g_idle_add (refresh_cache, NULL);

    gtk_main ();

    return 0;
}

/* End of file                                                                */
/*----------------------------------------------------------------------------*/
