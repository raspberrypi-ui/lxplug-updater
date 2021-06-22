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

//#define DEBUG_ON
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
} UpdaterPlugin;

/* Prototypes */

static void updater_popup_set_position (GtkMenu *menu, gint *px, gint *py, gboolean *push_in, gpointer data);
static void update_icon (UpdaterPlugin *up);
static gboolean idle_icon_update (gpointer data);
static void show_menu (UpdaterPlugin *up);
static void hide_menu (UpdaterPlugin *up);

static void check_for_updates (gpointer user_data)
{
    UpdaterPlugin *up = (UpdaterPlugin *) user_data;
}

static void show_updates (GtkWidget *widget, gpointer user_data)
{
    UpdaterPlugin *up = (UpdaterPlugin *) user_data;
}

static void install_updates (GtkWidget *widget, gpointer user_data)
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
