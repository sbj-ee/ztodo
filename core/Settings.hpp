#pragma once

#include <QString>
#include <QStringList>

namespace ztodo {

// Per-device settings: ~/.config/ztodo/ztodo.ini (macOS: ~/Library/Preferences/ztodo/).
struct AppSettings {
    QString dataDir;     // the shared folder inside Box; empty until set up
    QString syncCommand; // optional, run after changes and on Sync Now (Linux rclone)
    bool checkForUpdatesOnStartup = true;
    bool remindersEnabled = true; // desktop notification when a task with a time is due
    int reminderLeadMinutes = 0;  // 0 = at the due time

    static QString filePath();
    static AppSettings load();
    void save() const;
};

// This installation's id, created once: "<hostname>-<8 random chars>". It
// names this device's log in the data folder, so it must never be shared by
// two machines (don't copy ~/.config/ztodo between them).
QString deviceId();

// Box folders that exist on this machine, best first:
//   macOS: ~/Library/CloudStorage/Box-Box (Box Drive), ~/Box, ~/Box Sync
//   Linux: ~/Box, ~/box (an rclone bisync or mount)
QStringList boxRoots();
// <first Box root>/ztodo, or ~/ztodo when no Box folder is found.
QString suggestedDataDir();
// Linux with a boxsync user unit (rclone bisync): a command that starts a sync
// without waiting for it. Empty otherwise.
QString suggestedSyncCommand();

} // namespace ztodo
