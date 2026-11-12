#include "ConfigPaths.hpp"
#include "MainWindow.hpp"
#include "Settings.hpp"
#include "SettingsDialog.hpp"
#include "Store.hpp"
#include "UpdateManager.hpp"
#include "version.hpp"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QTimer>

#include <cstdio>
#include <cstring>

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            std::printf("ztodo %s\n", ztodo::kVersionString);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf("Usage: ztodo [--version]\nA Todoist-style task manager; tasks sync through a Box folder.\n");
            return 0;
        }
    }

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ztodo"));
    QApplication::setApplicationName(QStringLiteral("ztodo"));
    QApplication::setApplicationVersion(QString::fromLatin1(ztodo::kVersionString));
    QApplication::setDesktopFileName(QStringLiteral("ztodo"));
    QIcon icon;
    for (int s : {16, 24, 32, 48, 64, 128, 256, 512}) {
        icon.addFile(QStringLiteral(":/icons/ztodo-%1.png").arg(s), QSize(s, s));
    }
    QApplication::setWindowIcon(icon);

    // One ztodo per device: two would append to the same log at once.
    QDir().mkpath(ztodo::configDir());
    QLockFile lock(ztodo::configDir() + QStringLiteral("/ztodo.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(200)) {
        QMessageBox::information(nullptr, QStringLiteral("ztodo"), QStringLiteral("ztodo is already running."));
        return 0;
    }

    ztodo::AppSettings settings = ztodo::AppSettings::load();
    ztodo::Store store;
    bool welcome = settings.dataDir.isEmpty();
    for (;;) {
        if (welcome) {
            ztodo::SettingsDialog dlg(settings, true);
            if (dlg.exec() != QDialog::Accepted) {
                return 0;
            }
            settings = dlg.result();
        }
        QString err;
        if (store.open(settings.dataDir, ztodo::deviceId(), &err)) {
            settings.save();
            break;
        }
        QMessageBox::warning(nullptr, QStringLiteral("ztodo"), err);
        welcome = true;
    }

    // Keep this device's log small: drop edits that newer ones replaced.
    if (QFileInfo(store.ownLogPath()).size() > 256 * 1024) {
        store.compactOwnLog();
    }

    ztodo::MainWindow w(&store);
    w.show();
    QTimer::singleShot(500, &w, [&w] { w.runSyncCommand(); });
    ztodo::UpdateManager::instance().scheduleStartupCheck(&w);
    return QApplication::exec();
}
