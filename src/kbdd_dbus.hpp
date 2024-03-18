#ifndef TRAY_WATCHER_HPP
#define TRAY_WATCHER_HPP

#include <memory>
#include <giomm.h>
#include <glibmm.h>
#include <wayfire/util.hpp>
#include <wayfire/signal-definitions.hpp>

/*
 * Singleton representing a StatusNotifierWatceher instance.
 */
class KbddDBus
{
  public:
    static constexpr auto SNW_PATH = "/org/wayfire/kbdd/layout";
    static constexpr auto SNW_NAME = "org.wayfire.kbdd.layout";
    static constexpr auto SNW_IFCE = SNW_NAME;

    static std::shared_ptr<KbddDBus> Launch(
        std::function<void(bool)> enable_cb,
        std::function<bool(std::string)> switch_cb);
    static std::shared_ptr<KbddDBus> Instance();

    void set_switch_callback(std::function<bool(std::string)>);
    void set_enable_callback(std::function<void(bool)>);

    ~KbddDBus();

  private:
    inline static std::weak_ptr<KbddDBus> instance;

    std::function<void(bool)> enable_callback;
    std::function<bool(std::string)> switch_callback;

    wf::wl_timer timer;
    Glib::RefPtr<Glib::MainLoop> main_loop;

    guint dbus_name_id;
    guint dbus_object_id;
    Glib::RefPtr<Gio::DBus::Connection> kbdd_dbus_connection;

    const Gio::DBus::InterfaceVTable interface_table = Gio::DBus::InterfaceVTable(
            sigc::mem_fun(*this, &KbddDBus::handle_method_call),
            sigc::mem_fun(*this, &KbddDBus::handle_get_property),
            sigc::mem_fun(*this, &KbddDBus::handle_set_property)
        );

    KbddDBus();

    void on_bus_acquired(const Glib::RefPtr<Gio::DBus::Connection> & connection, const Glib::ustring & name);

    void handle_method_call(
        const Glib::RefPtr<Gio::DBus::Connection> & connection,
        const Glib::ustring & sender,
        const Glib::ustring & object_path,
        const Glib::ustring & interface_name,
        const Glib::ustring & method_name,
        const Glib::VariantContainerBase & parameters,
        const Glib::RefPtr<Gio::DBus::MethodInvocation> & invocation);

    void handle_get_property(
        Glib::VariantBase & property,
        const Glib::RefPtr<Gio::DBus::Connection> & connection,
        const Glib::ustring &sender,
        const Glib::ustring &object_path,
        const Glib::ustring &interface_name,
        const Glib::ustring &property_name);

    bool handle_set_property(
        const Glib::RefPtr<Gio::DBus::Connection> &connection,
        const Glib::ustring &sender,
        const Glib::ustring &object_path,
        const Glib::ustring &interface_name,
        const Glib::ustring &property_name,
        const Glib::VariantBase &value);

#if 0
    template<typename... Args>
    void emit_signal(const Glib::ustring & name, Args &&... args)
    {
        watcher_connection->emit_signal(
            SNW_PATH, SNW_IFACE, name, {},
            Glib::Variant<std::tuple<std::remove_cv_t<std::remove_reference_t<Args>> ...>>::create(
                std::tuple(std::forward<Args>(args)...)));
    }

    void emit_signal(const Glib::ustring& name)
    {
        watcher_connection->emit_signal(SNW_PATH, SNW_IFACE, name);
    }
#endif
}; // namespace KbddDBus

#endif
