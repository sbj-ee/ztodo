#include "Settings.hpp"

#include "ConfigPaths.hpp"
#include "Store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QSysInfo>

namespace ztodo {

QString AppSettings::filePath()
{
    return configDir() + QStringLiteral("/ztodo.ini");
}

AppSettings AppSettings::load()
{
    QSettings s(filePath(), QSettings::IniFormat);
    AppSettings a;
    a.dataDir = s.value(QStringLiteral("data/dir")).toString();
    a.syncCommand = s.value(QStringLiteral("data/syncCommand")).toString();
    a.checkForUpdatesOnStartup = s.value(QStringLiteral("updates/checkOnStartup"), true).toBool();
    return a;
}

void AppSettings::save() const
{
    QDir().mkpath(configDir());
    QSettings s(filePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("data/dir"), dataDir);
    s.setValue(QStringLiteral("data/syncCommand"), syncCommand);
    s.setValue(QStringLiteral("updates/checkOnStartup"), checkForUpdatesOnStartup);
    s.sync();
}

QString deviceId()
{
    const QString path = configDir() + QStringLiteral("/device-id");
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const QString id = QString::fromUtf8(f.readAll()).trimmed();
        if (!id.isEmpty()) {
            return id;
        }
    }
    QString host = QSysInfo::machineHostName().toLower();
    QString clean;
    for (QChar c : host) {
        if ((c.isLetterOrNumber() && c.unicode() < 128) || c == QLatin1Char('-')) {
            clean += c;
        }
    }
    clean = clean.left(24);
    if (clean.isEmpty()) {
        clean = QStringLiteral("device");
    }
    const QString id = clean + QLatin1Char('-') + newId().left(8).toLower();
    QDir().mkpath(configDir());
    QSaveFile out(path);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(id.toUtf8() + '\n');
        out.commit();
    }
    return id;
}

QStringList boxRoots()
{
    const QString home = QDir::homePath();
    QStringList candidates;
#if defined(Q_OS_MACOS)
    candidates << home + QStringLiteral("/Library/CloudStorage/Box-Box") << home + QStringLiteral("/Box")
               << home + QStringLiteral("/Box Sync");
#else
    candidates << home + QStringLiteral("/Box") << home + QStringLiteral("/box");
#endif
    QStringList out;
    for (const QString &c : candidates) {
        if (QFileInfo(c).isDir()) {
            out << c;
        }
    }
    return out;
}

QString suggestedDataDir()
{
    const QStringList roots = boxRoots();
    return (roots.isEmpty() ? QDir::homePath() : roots.first()) + QStringLiteral("/ztodo");
}

QString suggestedSyncCommand()
{
#if defined(Q_OS_LINUX)
    const QString unit = configHome() + QStringLiteral("/systemd/user/boxsync.service");
    if (QFileInfo::exists(unit)) {
        return QStringLiteral("systemctl --user start --no-block boxsync.service");
    }
#endif
    return {};
}

} // namespace ztodo
