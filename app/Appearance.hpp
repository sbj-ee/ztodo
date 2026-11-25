#pragma once

#include <QPalette>
#include <QString>

namespace ztodo::appearance {

// Light/dark choice. System follows the desktop; the other two force a theme
// (useful where the desktop doesn't tell Qt its scheme, so ztodo would
// otherwise always look light).
enum class Mode { System, Light, Dark };

Mode parse(const QString &s); // "system" | "light" | "dark"; anything else: System
QString toString(Mode m);

QPalette lightPalette();
QPalette darkPalette();

// Applies the mode to the running application. The first call remembers the
// system style and palette so System can be restored later. System at startup
// changes nothing, so the app keeps following live desktop theme changes; after
// switching from Light/Dark back to System, the system look is restored but
// follows the desktop again only after a restart.
void apply(Mode m);

// Gives every dialog (message boxes, Preferences, Edit Task, …) a background
// of its own: a raised (lighter) grey on a dark palette, a slightly darker
// grey on a light one. With the window's own colour, a dialog over the main
// window is hard to see on desktops that draw no strong frame or shadow
// around it. Derived from the current application palette, so it is called
// again after every palette change (apply() does; MainWindow does for desktop
// theme switches). Not on the native macOS style, which draws its own dialog
// surfaces.
void styleDialogs();

} // namespace ztodo::appearance
