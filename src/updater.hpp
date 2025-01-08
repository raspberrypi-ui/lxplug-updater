#ifndef WIDGETS_UPDATER_HPP
#define WIDGETS_UPDATER_HPP

#include <widget.hpp>
#include <gtkmm/button.h>

extern "C" {
#include "updater.h"
extern void updater_init (UpdaterPlugin *up);
extern void updater_update_display (UpdaterPlugin *up);
extern void updater_set_interval (UpdaterPlugin *up);
extern gboolean updater_control_msg (UpdaterPlugin *up, const char *cmd);
extern void updater_destructor (gpointer user_data);}

class WayfireUpdater : public WayfireWidget
{
    std::unique_ptr <Gtk::Button> plugin;

    WfOption <int> icon_size {"panel/icon_size"};
    WfOption <std::string> bar_pos {"panel/position"};
    sigc::connection icon_timer;

    WfOption <int> interval {"panel/updater_interval"};

    /* plugin */
    UpdaterPlugin *up;

  public:

    void init (Gtk::HBox *container) override;
    void command (const char *cmd) override;
    virtual ~WayfireUpdater ();
    void icon_size_changed_cb (void);
    void bar_pos_changed_cb (void);
    bool set_icon (void);
    void settings_changed_cb (void);
};

#endif /* end of include guard: WIDGETS_UPDATER_HPP */
