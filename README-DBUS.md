# DBus integration with `wf-panel-pi` (and possibly other shells)

## What is kbdd?

The `kbdd` plugin works independently of any other programs, keeping track of
the current window (*view*, in wayland/wayfire terminology) and its selected
keyboard language (*layout* in X11/Wayland jargon). Each time focus is changed
to another view, `kbdd` stores the layout of this view, and upon a later re-focus, restores it.

In addition to that, `kbdd` can integrate with DBus to exchange messages with
a *shell* application, so that the latter can display the current keyboard
layout and also switch keyboard layouts. The rest of this README give some
more information on this.

## What is DBus integration?

`Kbdd`'s job is fairly simple, and should not call for this entire README
file to explain. However, in a desktop environment like Wayfire, there is
also what is usually called the *shell* application, which is a special
app for launching other apps, switching among them, and displaying various
statuses. It usually takes the form of a taskbar on the top, bottom, left
or right of the screen.

DBus integration allows `kbdd` to interact with a shell (usually, an
*applet* or *widget*, or whatever it is called, in this shell). Interaction
is about two things:

- displaying the currently selected and the available language(s) (layouts)
  in the designated applet of the shell
- receiving commands to enable, to disable, and to actually perform
  switching among languages (layouts)

This takes the form of a simple protocol over the DBus communications
bus that all modern Linux distros offer.

## Why does it have to be so complicated?

In Wayland (the underlying protocol for Wayfire), there is an isolation
between the the windowing system (the *compositor*) and applications
(views) and between the views themselves. Conceptually, each view has
its own keybaord and, for security reasons, there is no application that
can snoop or steal the keyboard of another view (while an application
can "grab" the keyboard of the whole screen, or provide a virtual
on-screen keyboard for other applications, neither of these is what
we are looking for).

In other words, the shell (which is just another application among others),
can neither peek the currently selected language of another view, nor
instruct another view to switch its currently selected keyboard language
(layout).

Therefore, the only reasonable way of making a language display/switch
applet work is by creating a direct communication channel between the
applet (part of the shell) and the compositor itself (in this case,
wayfire, or, more precisely, our `kbdd` plugin that lives within wayfire),
which is the only entity that is entitled to and capable of getting the
current keyboard language for each view and switching among languages.

## How does the DBus integration work?

There are two modes in which a DBus integration between `kbdd` and the
shell can work: *one-way* or *two-way*.

In one-way integration, `kbdd` just informs the shell on the currently
selected language.  For this to work with Raspberry Pi's default shell
(`wf-panel-pi`), `kbdd` uses a small script bundled with `wf-panel-pi`,
called `wfpanelctl` to send DBus commands to a shell applet that can
then use them to display the current language.

In two-way integration, things take a few more steps:
- first, `kbdd` reads the configured layouts from `wayfire.ini` (notably,
  it can re-do this on-the-fly, while wayfire is running, if the file is
  changed)
- second, `kbdd` sends this list to `wf-panel-pi`, which then can display
  (in its designated language applet) a menu of layouts
- third, `kbdd` awaits an "enable" command from the shell
- forth, after the "enable" command has been received, `kbdd` is ready
  to receive simple "switch" commands to switch between layouts.

This last step may be a bit more complicated than it sounds; before a
menu-click-driven language switch can take place, the shell has taken
the focus from the view whose layout we want to change. After the user
clicks on a menu item to select a language, the focus returns to that
view, and a DBus command is sent from the shell applet to `kbdd`;
these two events occur asynchronously in parllel, so there is no hard
guarantee as to which of the two will reach `kbdd` first. For this reason,
`kbdd` has a more complicated logic to keep track of the last view previous
to the shell, to make sure that it does not switch the layout of the shell
itself instead of that of the intended (application) view.

## What is the DBUS protocol for this?

The respective DBus introspection XML is as follows:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<node>
  <interface name="org.wayfire.kbdd.layout">
    <!--
        The "changed" signal is for notifying other apps about a keyboard
        layout change; currently, this signal is not implemented
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

```

