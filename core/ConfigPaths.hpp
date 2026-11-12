#pragma once

#include <QFile>
#include <QStandardPaths>
#include <QString>

namespace ztodo {

// Config root for settings, device id and update state (NOT the task data,
// which lives in the Box folder). Honours $XDG_CONFIG_HOME when set (tests and
// Linux); otherwise QStandardPaths::GenericConfigLocation (~/.config on Linux,
// ~/Library/Preferences on macOS).
inline QString configHome()
{
    const QByteArray xdg = qgetenv("XDG_CONFIG_HOME");
    if (!xdg.isEmpty()) {
        return QFile::decodeName(xdg);
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

inline QString configDir()
{
    return configHome() + QStringLiteral("/ztodo");
}

} // namespace ztodo
