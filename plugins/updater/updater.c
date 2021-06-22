/*
Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
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
#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>

#include "plugin.h"

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

#define DEBUG_ON
#ifdef DEBUG_ON
#define DEBUG(fmt,args...) g_message("up: " fmt,##args)
#else
#define DEBUG
#endif

/* Plug-in global data */

typedef struct {

    GtkWidget *plugin;              /* Back pointer to the widget */
    LXPanel *panel;                 /* Back pointer to panel */
    GtkWidget *tray_icon;           /* Displayed image */
    config_setting_t *settings;     /* Plugin settings */
    GtkWidget *menu;                /* Popup menu */
    gboolean updates_avail;
    int calls;
    gchar **ids;
    gboolean is_pi;
} UpdaterPlugin;

/* Prototypes */

static void updater_popup_set_position (GtkMenu *menu, gint *px, gint *py, gboolean *push_in, gpointer data);
static void update_icon (UpdaterPlugin *up);
static gboolean idle_icon_update (gpointer data);
static void show_menu (UpdaterPlugin *up);
static void hide_menu (UpdaterPlugin *up);

static char *get_shell_string (char *cmd, gboolean all)
{
    char *line = NULL, *res = NULL;
    size_t len = 0;
    FILE *fp = popen (cmd, "r");

    if (fp == NULL) return g_strdup ("");
    if (getline (&line, &len, fp) > 0)
    {
        g_strdelimit (line, "\n\r", 0);
        if (!all)
        {
            res = line;
            while (*res++) if (g_ascii_isspace (*res)) *res = 0;
        }
        res = g_strdup (line);
    }
    pclose (fp);
    g_free (line);
    return res ? res : g_strdup ("");
}

static char *get_string (char *cmd)
{
    return get_shell_string (cmd, FALSE);
}

static gboolean net_available (void)
{
    char *ip;
    gboolean val = FALSE;

    ip = get_string ("hostname -I | tr ' ' \\\\n | grep \\\\. | tr \\\\n ','");
    if (ip)
    {
        if (strlen (ip)) val = TRUE;
        g_free (ip);
    }
    return val;
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

static void resync (void)
{
    if (system ("test -e /usr/sbin/ntpd") == 0)
    {
        system ("/etc/init.d/ntp stop; ntpd -gq; /etc/init.d/ntp start");
    }
    else
    {
        system ("systemctl -q stop systemd-timesyncd 2> /dev/null; systemctl -q start systemd-timesyncd 2> /dev/null");
    }
}

static gboolean filter_fn (PkPackage *package, gpointer user_data)
{
    UpdaterPlugin *up = (UpdaterPlugin *) user_data;
    if (up->is_pi) return TRUE;
    if (strstr (pk_package_get_arch (package), "amd64")) return FALSE;
    return TRUE;
}

static void check_updates_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    UpdaterPlugin *up = (UpdaterPlugin *) data;
    PkPackageSack *sack, *fsack;
    int n_up;

    GError *error = NULL;
    PkResults *results = pk_task_generic_finish (task, res, &error);

    if (error != NULL)
    {
        DEBUG ("Error comparing versions - %s", error->message);
        g_error_free (error);
        return;
    }

    sack = pk_results_get_package_sack (results);
    fsack = pk_package_sack_filter (sack, filter_fn, data);

    n_up = pk_package_sack_get_size (fsack);
    if (n_up > 0)
    {
        DEBUG ("Check complete - %d updates available", n_up);
        up->updates_avail = TRUE;
    }
    else
    {
        DEBUG ("Check complete - no updates available");
        up->updates_avail = FALSE;
    }
    update_icon (up);

    g_object_unref (sack);
    g_object_unref (fsack);
}

static void refresh_cache_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    GError *error = NULL;
    PkResults *results = pk_task_generic_finish (task, res, &error);

    if (error != NULL)
    {
        DEBUG ("Error updating cache - %s", error->message);
        g_error_free (error);
        return;
    }

    DEBUG ("Cache updated - comparing versions");
    pk_client_get_updates_async (PK_CLIENT (task), PK_FILTER_ENUM_NONE, NULL, NULL, NULL, (GAsyncReadyCallback) check_updates_done, data);
}

static gpointer refresh_update_cache (gpointer data)
{
    PkTask *task = pk_task_new ();
    pk_client_refresh_cache_async (PK_CLIENT (task), TRUE, NULL, NULL, NULL, (GAsyncReadyCallback) refresh_cache_done, data);
    return NULL;
}

static gboolean ntp_check (gpointer data)
{
    UpdaterPlugin *up = (UpdaterPlugin *) data;

    if (clock_synced ())
    {
        DEBUG ("Clock synced - checking for updates");
        g_thread_new (NULL, refresh_update_cache, up);
        return FALSE;
    }

    if (up->calls == 0) resync ();

    if (up->calls++ > 120)
    {
        DEBUG ("Couldn't sync clock - update check failed");
        return FALSE;
    }

    return TRUE;
}

static void check_for_updates (gpointer user_data)
{
    UpdaterPlugin *up = (UpdaterPlugin *) user_data;

    if (!net_available ())
    {
        DEBUG ("No network connection - update check failed");
        return;
    }

    if (!clock_synced ())
    {
        DEBUG ("Synchronising clock");
        up->calls = 0;
        g_timeout_add_seconds (1, ntp_check, up);
        return;
    }

    DEBUG ("Checking for updates");
    g_thread_new (NULL, refresh_update_cache, up);
}

static void install_updates (GtkWidget *widget, gpointer user_data)
{
    UpdaterPlugin *up = (UpdaterPlugin *) user_data;
    //pk_task_update_packages_async (task, up->ids, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) do_updates_done, user_data);
    //g_strfreev (up->ids);
}

static void show_updates (GtkWidget *widget, gpointer user_data)
{
    UpdaterPlugin *up = (UpdaterPlugin *) user_data;
}

/* Updater functions */

static void updater_popup_set_position (GtkMenu *menu, gint *px, gint *py, gboolean *push_in, gpointer data)
{
    UpdaterPlugin *up = (UpdaterPlugin *) data;
    /* Determine the coordinates. */
    lxpanel_plugin_popup_set_position_helper (up->panel, up->plugin, GTK_WIDGET (menu), px, py);
    *push_in = TRUE;
}

static void update_icon (UpdaterPlugin *up)
{
    /* if updates are available, show the icon */
    if (up->updates_avail)
    {
        gtk_widget_show_all (up->plugin);
        gtk_widget_set_sensitive (up->plugin, TRUE);
    }
    else
    {
        gtk_widget_hide (up->plugin);
        gtk_widget_set_sensitive (up->plugin, FALSE);
    }
}

static gboolean init_icon (gpointer data)
{
    UpdaterPlugin *up = (UpdaterPlugin *) data;
    update_icon (up);
    check_for_updates (up);
    return FALSE;
}

static void show_menu (UpdaterPlugin *up)
{
    GtkWidget *item;

    hide_menu (up);

    up->menu = gtk_menu_new ();
    gtk_menu_set_reserve_toggle_size (GTK_MENU (up->menu), FALSE);

    item = lxpanel_plugin_new_menu_item (up->panel, _("Show Updates..."), 0, NULL);
    g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (show_updates), up);
    gtk_menu_shell_append (GTK_MENU_SHELL (up->menu), item);

    item = lxpanel_plugin_new_menu_item (up->panel, _("Install Updates"), 0, NULL);
    g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (install_updates), up);
    gtk_menu_shell_append (GTK_MENU_SHELL (up->menu), item);

    gtk_widget_show_all (up->menu);
    gtk_menu_popup_at_widget (GTK_MENU (up->menu), up->plugin, GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

static void hide_menu (UpdaterPlugin *up)
{
    if (up->menu)
    {
		gtk_menu_popdown (GTK_MENU (up->menu));
		gtk_widget_destroy (up->menu);
		up->menu = NULL;
	}
}

/* Handler for menu button click */
static gboolean updater_button_press_event (GtkWidget *widget, GdkEventButton *event, LXPanel *panel)
{
    UpdaterPlugin *up = lxpanel_plugin_get_data (widget);

#ifdef ENABLE_NLS
    textdomain (GETTEXT_PACKAGE);
#endif
    /* Show or hide the popup menu on left-click */
    if (event->button == 1)
    {
        show_menu (up);
        return TRUE;
    }
    else return FALSE;
}

/* Handler for system config changed message from panel */
static void updater_configuration_changed (LXPanel *panel, GtkWidget *p)
{
    UpdaterPlugin *up = lxpanel_plugin_get_data (p);

    lxpanel_plugin_set_taskbar_icon (panel, up->tray_icon, "dialog-warning-symbolic");
}

/* Plugin destructor. */
static void updater_destructor (gpointer user_data)
{
    UpdaterPlugin *up = (UpdaterPlugin *) user_data;

    /* Deallocate memory */
    g_free (up);
}

/* Plugin constructor. */
static GtkWidget *updater_constructor (LXPanel *panel, config_setting_t *settings)
{
    /* Allocate and initialize plugin context */
    UpdaterPlugin *up = g_new0 (UpdaterPlugin, 1);
    int val;

#ifdef ENABLE_NLS
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
#endif

    if (system ("raspi-config nonint is_pi")) up->is_pi = FALSE;
    else up->is_pi = TRUE;

    up->tray_icon = gtk_image_new ();
    lxpanel_plugin_set_taskbar_icon (panel, up->tray_icon, "dialog-warning-symbolic");
    gtk_widget_set_tooltip_text (up->tray_icon, _("Updates are available - click the icon to install"));
    gtk_widget_set_visible (up->tray_icon, TRUE);

    /* Allocate top level widget and set into Plugin widget pointer. */
    up->panel = panel;
    up->plugin = gtk_button_new ();
    gtk_button_set_relief (GTK_BUTTON (up->plugin), GTK_RELIEF_NONE);
    g_signal_connect (up->plugin, "button-press-event", G_CALLBACK (updater_button_press_event), NULL);
    up->settings = settings;
    lxpanel_plugin_set_data (up->plugin, up, updater_destructor);
    gtk_widget_add_events (up->plugin, GDK_BUTTON_PRESS_MASK);

    /* Allocate icon as a child of top level */
    gtk_container_add (GTK_CONTAINER (up->plugin), up->tray_icon);

    /* Initialise data structures */
    up->menu = NULL;
    up->updates_avail = FALSE;

    /* Hide the widget and start the check for updates */
    gtk_widget_show_all (up->plugin);
    g_idle_add (init_icon, up);
    return up->plugin;
}

static gboolean updater_apply_configuration (gpointer user_data)
{
    UpdaterPlugin *up = lxpanel_plugin_get_data ((GtkWidget *) user_data);

    update_icon (up);
}

static GtkWidget *updater_configure (LXPanel *panel, GtkWidget *p)
{
    UpdaterPlugin *up = lxpanel_plugin_get_data (p);
#ifdef ENABLE_NLS
    textdomain (GETTEXT_PACKAGE);
#endif
    return lxpanel_generic_config_dlg(_("Updater"), panel,
        updater_apply_configuration, p,
//        _("Hide icon when no devices"), &up->autohide, CONF_TYPE_BOOL,
        NULL);
}

FM_DEFINE_MODULE(lxpanel_gtk, updater)

/* Plugin descriptor. */
LXPanelPluginInit fm_module_init_lxpanel_gtk = {
    .name = N_("Updater"),
    .description = N_("Checks for updates"),
    .new_instance = updater_constructor,
    .reconfigure = updater_configuration_changed,
    .button_press_event = updater_button_press_event,
    .config = updater_configure,
    .gettext_package = GETTEXT_PACKAGE
};
