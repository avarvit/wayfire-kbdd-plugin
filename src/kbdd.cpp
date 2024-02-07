#include <xkbcommon/xkbcommon.h>
#include <wayfire/plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>


struct view_layout_id : public wf::custom_data_t
{
    int layout_id;

    view_layout_id(int id) {
        this->layout_id = id;
    };

    virtual ~view_layout_id() = default;
};

class tokenized_string : public wf::custom_data_t
{
    std::vector<std::string> tsvector;

public:
    tokenized_string() {
    }

    tokenized_string(std::string str, std::string sep, bool cap) {
        size_t last = 0, next = 0;
        std::string l;
        while ((next = str.find(sep, last)) != std::string::npos) {
            l = str.substr(last, next - last);
            if (cap) {
                for (auto & c : l) c = toupper(c);
            }
            tsvector.push_back(l);
            last = next + 1;
        }
        l = str.substr(last);
        if (cap) {
            for (auto & c : l) c = toupper(c);
        }
        tsvector.push_back(l);
    }

    const char *element(int index) {
        if (index < 0 || (unsigned int)index >= tsvector.size()) {
            return("--");
        }
        return tsvector.at(index).c_str();
    }

    virtual ~tokenized_string() = default;
};



class kbdd_plugin : public wf::plugin_interface_t
{

    uint prev_view_id = -1;
    const int default_layout_id = 0;
    const char *view_layout_id_key = "keyboard-layout-id";
    std::string notify_command = "wfpanelctl kbdlayout ";
    wf::wl_timer timer;

    // layout names and state
    tokenized_string short_layouts;
    // char **long_layout_names;
    int prev_layout_id = -1;

    // largely a copy of wayfire's sea/keyboard.hpp
    wf::option_wrapper_t<std::string> model, variant, layout, options, rules;
    bool dirty_options = true;

    // we could just as well get the layout names from the current keymap; however,
    // while we can get long names from xkb, there is no backwards mapping, alas;
    // thus, we load here again xkb_options from wayfire.ini, to grab short names

    void load_xkb_options() {
        model.load_option("input/xkb_model");
        variant.load_option("input/xkb_variant");
        layout.load_option("input/xkb_layout");
        options.load_option("input/xkb_options");
        rules.load_option("input/xkb_rules");

        // When the configuration options change, mark them as dirty.
        // They are applied at the config-reloaded signal.
        model.set_callback([=] () { this->dirty_options = true; });
        variant.set_callback([=] () { this->dirty_options = true; });
        layout.set_callback([=] () { this->dirty_options = true; });
        options.set_callback([=] () { this->dirty_options = true; });
        rules.set_callback([=] () { this->dirty_options = true; });

        reload_options();
    }

    void reload_options() {
        if (!this->dirty_options) {
            return;
        }

        timer.disconnect();

        auto ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        /* Copy memory to stack, so that .c_str() is valid */
        std::string rules   = this->rules;
        std::string model   = this->model;
        std::string layout  = this->layout;
        std::string variant = this->variant;
        std::string options = this->options;

        xkb_rule_names names;
        names.rules   = rules.c_str();
        names.model   = model.c_str();
        names.layout  = layout.c_str();
        names.variant = variant.c_str();
        names.options = options.c_str();
        auto keymap = xkb_map_new_from_names(ctx, &names,
                                             XKB_KEYMAP_COMPILE_NO_FLAGS);

        if (!keymap) {
            // TODO: maybe handle better?
            xkb_context_unref(ctx);
            return;
        }

        short_layouts = tokenized_string(layout, ",", true);

        xkb_keymap_unref(keymap);
        xkb_context_unref(ctx);

        this->dirty_options = false;
        this->prev_layout_id = current_keyboard_layout_id();
        timer.set_timeout(4000, [=] () {
            run_notify_panel_command(this->current_keyboard_layout_id());
            return true;
        });
        run_notify_panel_command(this->prev_layout_id);
    }

    void run_notify_panel_command(int layout_id) {
        wf::get_core().run(notify_command + this->short_layouts.element(layout_id));
    }

    int current_keyboard_layout_id() {
        wlr_seat *seat = wf::get_core().get_current_seat();
        wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
        if (!keyboard)
            return -1;
        return xkb_state_serialize_layout(keyboard->xkb_state,
                                           XKB_STATE_LAYOUT_LOCKED);
    }

    int get_view_layout(wayfire_view view) {
        if (view && view->has_data(view_layout_id_key)) {
            return view->get_data<view_layout_id>(view_layout_id_key)->layout_id;
        }

        return default_layout_id;
    }


    void restore_view_layout(wayfire_view view, wlr_keyboard *keyboard) {
        int layout_id = get_view_layout(view);

        if (layout_id != prev_layout_id) {
            run_notify_panel_command(layout_id);
            prev_layout_id = layout_id;
        }

        wlr_keyboard_notify_modifiers(keyboard,
                                      keyboard->modifiers.depressed,
                                      keyboard->modifiers.latched,
                                      keyboard->modifiers.locked,
                                      layout_id);
    };


    void save_view_layout(uint view_id, wlr_keyboard *keyboard) {
        if (view_id > 0) {
            int layout_id = xkb_state_serialize_layout(keyboard->xkb_state,
                                                       XKB_STATE_LAYOUT_LOCKED);

            for (auto v : wf::get_core().get_all_views()) {
                if (v->get_id() == view_id) {
                    v->store_data(std::make_unique<view_layout_id>(layout_id),
                                  view_layout_id_key);
                    break;
                }
            }
        }
    };


    wf::signal_connection_t reload_config = [=] (wf::signal_data_t *data) {

        (void) data; // suppress unused warning
        reload_options();

    };


    wf::signal_connection_t keyboard_focus_changed = [=] (wf::signal_data_t *data) {

        wf::keyboard_focus_changed_signal *signal = static_cast<wf::keyboard_focus_changed_signal*>(data);
        wayfire_view view = signal->view;

        if (!view)
            return;

        wlr_seat *seat = wf::get_core().get_current_seat();
        wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
        if (!keyboard)
            return;

        save_view_layout(prev_view_id, keyboard);

        int view_id = view->get_id();
        prev_view_id = view_id;

        restore_view_layout(view, keyboard);
    };

    wf::signal_connection_t keyboard_key_post = [=] (wf::signal_data_t *data) {

        (void) data; // suppress unused warning
        // wlr_keyboard_key_event *ev = (wlr_keyboard_key_event*)(data);

        int layout_id = current_keyboard_layout_id();
        if (layout_id < 0)
            return;

        if (layout_id != prev_layout_id) {
            run_notify_panel_command(layout_id);
            prev_layout_id = layout_id;
        }
    };


public:
    void init() override
    {
        load_xkb_options();

        wf::get_core().connect_signal("reload-config", &reload_config);
        wf::get_core().connect_signal("keyboard-focus-changed", &keyboard_focus_changed);
        wf::get_core().connect_signal("keyboard_key_post", &keyboard_key_post);
    }

    void fini() override {
        timer.disconnect();
        wf::get_core().disconnect_signal(&keyboard_focus_changed);
        wf::get_core().disconnect_signal(&keyboard_key_post);
    }
};

DECLARE_WAYFIRE_PLUGIN(kbdd_plugin)
