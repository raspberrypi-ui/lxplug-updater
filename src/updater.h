typedef struct {
    GtkWidget *plugin;              /* Back pointer to the widget */
    GtkWidget *tray_icon;           /* Displayed image */
#ifdef LXPLUG
    LXPanel *panel;                 /* Back pointer to panel */
    config_setting_t *settings;     /* Plugin settings */
#else
    int icon_size;                      /* Variables used under wf-panel */
    gboolean bottom;
    GtkGesture *gesture;
#endif
    GtkWidget *menu;                /* Popup menu */
    GtkWidget *update_dlg;          /* Widget used to display pending update list */
    int n_updates;                  /* Number of pending updates */
    gchar **ids;                    /* ID strings for pending updates */
    int interval;                   /* Number of hours between periodic checks */
    guint timer;                    /* Periodic check timer ID */
    guint idle_timer;
    GCancellable *cancellable;
} UpdaterPlugin;


