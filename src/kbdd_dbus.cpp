#include "kbdd_dbus.hpp"

#include <iostream>

static const auto introspection_data = Gio::DBus::NodeInfo::create_for_xml(
    R"(
<?xml version="1.0" encoding="UTF-8"?>
<node>
  <interface name="org.wayfire.kbdd.layout">
    <!--
        The "changed" signal is for notifying other apps about a keyboard
        layout change; currently, it is not implemented
    -->
    <signal name="changed">
      <arg type="s" name="layout"/>
    </signal>
    <!--
        The "enable" method takes one unsigned integer (status) as argument;
        it is supposed to be called by a shell app to initialize or to stop
        kbdd's keyboard layout switching protocol; if "status" is non-zero,
        kbdd will henceforth honor "switch" commands; if "status" is zero,
        switching will be disabled (default state is desabled); kbdd keeps
        track of the last focus switch before this message, assuming that
        this last-focused-to view is the shell; it then keeps track of the
        focus switches to remember which was the last focused-to view
        besides the shell; it is this last-focused-to view that gets its
        keyboard changed by a subsequent "switch" command.
    -->
    <method name="enable">
      <arg type="u" name="status" direction="in"/>
    </method>
    <!--
        The "switch" method takes as an argument a string representing a
        keyboard layout (in capitalized xkbd 2-letter format, e.g., "US",
        "RU", "GR", etc.); if this layout is valid (in the sense that it
        is a configured xkbd layout), it is accepted, otherwise it is
        rejected; if accepted, the last-non-shell view gets its stored
        keyboard layout switched accordingly, so when focus returns to
        that view, the new layout applies.
    -->
    <method name="switch">
      <arg type="s" name="layout" direction="in"/>
    </method>
  </interface>
</node>
    )");

/*
try with:
dbus-send --session --dest=org.wayfire.kbdd.layout \
    --type=method_call /org/wayfire/kbdd/layout \
    org.wayfire.kbdd.layout.switch string:GR
*/


KbddDBus::KbddDBus() {
    Gio::init(); // also calls Glib::init() for us

    dbus_name_id = Gio::DBus::own_name(
        Gio::DBus::BusType::BUS_TYPE_SESSION,
        SNW_NAME,
        sigc::mem_fun(this, &KbddDBus::on_bus_acquired));
    main_loop = Glib::MainLoop::create();

    // in order to avoid the burden associated with creating a new thread to block
    // on Glib::MainLoop::run(), we iterate over the loop's context every 500ms;
    // we use the non-blocking version (iteration(FALSE)) to avoid blocking;
    // this is a bit of an overhead, and may also delay the response to a DBus
    // event up to 500ms, however if one takes into account that the source is
    // going to be a user-clicked layout-switching widget somewhere on a panel,
    // and then the user will have to point the mouse back to the previous
    // window, 500ms seems OK (to me, at least :-); at some later stage, when
    // I am done with everything else, I might add the code to write code for
    // creating, destructing and managing a purpose-specific thread in here

    timer.set_timeout(500, [=] () {
        if (main_loop->get_context()->iteration(FALSE)) {
        }
        return true;
    });
}

std::shared_ptr<KbddDBus> KbddDBus::Launch(
    std::function<void(bool)> enable_cb,
    std::function<bool(std::string)> switch_cb
) {
    if (instance.expired()) {
        auto kbdd_dbus_ptr = std::shared_ptr<KbddDBus>(new KbddDBus());
        instance = kbdd_dbus_ptr;
        Instance()->enable_callback = enable_cb;
        Instance()->switch_callback = switch_cb;
        return kbdd_dbus_ptr;
    }

    return Instance();
}

std::shared_ptr<KbddDBus> KbddDBus::Instance() {
    return instance.lock();
}

void KbddDBus::set_enable_callback(std::function<void(bool)> enable_cb) {
    enable_callback = enable_cb;
}

void KbddDBus::set_switch_callback(std::function<bool(std::string)> switch_cb) {
    switch_callback = switch_cb;
}

KbddDBus::~KbddDBus() {
#if 0
    for (const auto& [_, host_id] : sn_hosts_id) {
        Gio::DBus::unwatch_name(host_id);
    }

    for (const auto& [_, item_id] : sn_items_id) {
        Gio::DBus::unwatch_name(item_id);
    }

#endif
    timer.disconnect();
    kbdd_dbus_connection->unregister_object(dbus_object_id);
    Gio::DBus::unown_name(dbus_name_id);
}

void KbddDBus::on_bus_acquired(
    const Glib::RefPtr<Gio::DBus::Connection> & connection,
    const Glib::ustring & name)
{
    dbus_object_id = connection->register_object(
        SNW_PATH,
        introspection_data->lookup_interface(),
        interface_table);
    kbdd_dbus_connection = connection;

#ifndef USED
    (void) name;
#endif
}

#if 0
void KbddDBus::register_status_notifier_item(const Glib::RefPtr<Gio::DBus::Connection> & connection,
    const Glib::ustring & sender, const Glib::ustring & path)
{
    const auto full_obj_path = sender + path;
    emit_signal("StatusNotifierItemRegistered", full_obj_path);
    sn_items_id.emplace(full_obj_path, Gio::DBus::watch_name(
        Gio::DBus::BUS_TYPE_SESSION, sender, {},
        [this, full_obj_path] (const Glib::RefPtr<Gio::DBus::Connection> & connection,
                               const Glib::ustring & name)
    {
        Gio::DBus::unwatch_name(sn_items_id.at(full_obj_path));
        sn_items_id.erase(full_obj_path);
        emit_signal("StatusNotifierItemUnregistered", full_obj_path);
    }));
}
#endif

#if 0
void KbddDBus::register_status_notifier_host(const Glib::RefPtr<Gio::DBus::Connection> & connection,
    const Glib::ustring & service)
{
    sn_hosts_id.emplace(
        service, Gio::DBus::watch_name(
            connection, service,
            [this, is_host_registred_changed = sn_hosts_id.empty()] (
                const Glib::RefPtr<Gio::DBus::Connection> &, const Glib::ustring &, const Glib::ustring &)
    {
        emit_signal("StatusNotifierHostRegistered");
        if (is_host_registred_changed)
        {
            kbdd_dbus_connection->emit_signal(
                SNW_PATH, "org.freedesktop.DBus.Properties", "PropertiesChanged", {},
                Glib::Variant<std::tuple<Glib::ustring, std::map<Glib::ustring, Glib::VariantBase>,
                    std::vector<Glib::ustring>>>::
                create({SNW_IFACE,
                    {{"IsStatusNotifierHostRegistered", Glib::Variant<bool>::create(true)}},
                    {}}));
        }
    },
            [this] (const Glib::RefPtr<Gio::DBus::Connection> & connection, const Glib::ustring & name)
    {
        Gio::DBus::unwatch_name(sn_hosts_id[name]);
        sn_hosts_id.erase(name);
    }));
}
#endif

void KbddDBus::handle_method_call(
    const Glib::RefPtr<Gio::DBus::Connection> & connection,
    const Glib::ustring & sender,
    const Glib::ustring & object_path,
    const Glib::ustring & interface_name,
    const Glib::ustring & method_name,
    const Glib::VariantContainerBase & parameters,
    const Glib::RefPtr<Gio::DBus::MethodInvocation> & invocation)
{
    if (method_name == "enable") {
        if (!parameters.is_of_type(Glib::VariantType("(u)"))) {
            std::cerr << "KbddDBus: invalid argument type: expected (u), got " <<
                parameters.get_type_string() <<
                std::endl;
            return;
        }
        Glib::Variant<guint32> enable_first_param_v;
        parameters.get_child(enable_first_param_v, 0);
        guint32 enable = enable_first_param_v.get();
        enable_callback((enable != 0));
    }
    else if (method_name == "switch") {
        if (!parameters.is_of_type(Glib::VariantType("(s)"))) {
            std::cerr << "KbddDBus: invalid argument type: expected (s), got " <<
                parameters.get_type_string() <<
                std::endl;
            return;
        }
        Glib::Variant<Glib::ustring> switch_first_param_v;
        parameters.get_child(switch_first_param_v, 0);
        Glib::ustring layout = switch_first_param_v.get();
        bool was_ok = switch_callback(layout);
        // avoid never-used warning
        (void) was_ok;
    }
    else {
        std::cerr << "StatusNotifierWatcher: unknown method " << method_name << std::endl;
        return;
    }
#if 0

    if (method_name == "RegisterStatusNotifierItem") {
        register_status_notifier_item(connection, service[0] == '/' ? sender : service,
            service[0] == '/' ? service : "/StatusNotifierItem");
    } else if (method_name == "RegisterStatusNotifierHost") {
        register_status_notifier_host(connection, service);
    } else {
        std::cerr << "StatusNotifierWatcher: unknown method " << method_name << std::endl;
        return;
    }

    invocation->return_value(Glib::VariantContainerBase());
#endif

#ifndef USED
    (void) connection;
    (void) sender;
    (void) object_path;
    (void) interface_name;
    (void) method_name;
    (void) parameters;
    (void) invocation;
#endif
}

void KbddDBus::handle_get_property(
    Glib::VariantBase & property,
    const Glib::RefPtr<Gio::DBus::Connection> & connection,
    const Glib::ustring & sender,
    const Glib::ustring & object_path,
    const Glib::ustring & interface_name,
    const Glib::ustring & property_name)
{
#if 0
    if (property_name == "RegisteredStatusNotifierItems") {
        property = get_registred_items();
    } else if (property_name == "IsStatusNotifierHostRegistered") {
        property = Glib::Variant<bool>::create(!sn_hosts_id.empty());
    } else if (property_name == "ProtocolVersion") {
        property = Glib::Variant<int>::create(0);
    } else {
        std::cerr << "StatusNotifierWatcher: Unknown property " << property_name << std::endl;
    }
#endif
#ifndef USED
    (void) property;
    (void) connection;
    (void) sender;
    (void) object_path;
    (void) interface_name;
    (void) property_name;
#endif
}


bool KbddDBus::handle_set_property(
    const Glib::RefPtr<Gio::DBus::Connection> &connection,
    const Glib::ustring &sender,
    const Glib::ustring &object_path,
    const Glib::ustring &interface_name,
    const Glib::ustring &property_name,
    const Glib::VariantBase &value)
{

#ifndef USED
    (void) connection;
    (void) sender;
    (void) object_path;
    (void) interface_name;
    (void) property_name;
    (void) value;
#endif

    return FALSE;
}


#if 0
Glib::Variant<std::vector<Glib::ustring>> Watcher::get_registred_items() const
{
    std::vector<Glib::ustring> sn_items_names;
    sn_items_names.reserve(sn_items_id.size());
    for (const auto & [service, id] : sn_items_id)
    {
        sn_items_names.push_back(service);
    }

    return Glib::Variant<std::vector<Glib::ustring>>::create(sn_items_names);
}
#endif
