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

} // namespace ztodo::appearance
