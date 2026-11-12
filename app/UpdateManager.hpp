#pragma once

#include "Update.hpp"
#include "UpdateChecker.hpp"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTemporaryDir>

#include <functional>
#include <memory>

class QProcess;
class QProgressDialog;
class QWidget;

namespace ztodo {

// One per process: Help > Check for Updates, the automatic check after
// startup (Preferences, at most once a day), the dialog and the install.
//
//   check GitHub's latest release -> offer (Install / Later / Skip)
//   -> download SHA256SUMS + SHA256SUMS.minisig, verify the signature against
//      the key compiled in, download the package, verify its SHA-256
//   -> Linux: pkexec apt install ./ztodo_<v>_amd64.deb
//      macOS: mount the .dmg, copy ztodo.app over the running bundle
//   -> offer to restart.
// Anything that can't be done automatically falls back to the verified
// package and the release page.
class UpdateManager : public QObject {
    Q_OBJECT
public:
    enum class Stage { Idle, Checking, Offering, Downloading, Installing, Done };
    Q_ENUM(Stage)

    static UpdateManager &instance();

    void checkNow(QWidget *parent);
    void scheduleStartupCheck(QWidget *parent, int delayMs = 4000);
    bool startupCheckIfDue(QWidget *parent);

    Stage stage() const { return m_stage; }
    QString lastMessage() const { return m_lastMessage; }
    UpdateChecker *checker() const { return m_checker; }
    void setCurrentVersionForTests(const QString &v) { m_currentVersion = v; }

    static QStringList installCommand(const QString &debPath);
    static QString stageVerifiedPackage(const QString &path, const QString &sha256, QTemporaryDir &into,
                                        QString *error);
#if defined(Q_OS_MACOS)
    // The .app bundle this process runs from, or empty when not in one.
    static QString runningBundle();
    // Mount the dmg, replace `bundle` with its ztodo.app. Empty on success.
    static QString replaceBundleFromDmg(const QString &dmg, const QString &bundle, const QString &workDir);
#endif

signals:
    void stageChanged(ztodo::UpdateManager::Stage stage);
    void checkFinished(bool offered);
    void installFinished(bool ok, const QString &message);

private:
    explicit UpdateManager(QObject *parent = nullptr);
    void startCheck(QWidget *parent, bool manual);
    void onCheckFinished(const UpdateChecker::Result &r);
    void offer();
    void startDownload();
    void onDownloadFinished(bool ok, const QString &pkgPath, const QString &error);
    void runInstaller();
    void onInstallerFinished(int exitCode, bool crashed);
    void installed();
    void restart();
    void fallback(const QString &why);
    void message(const QString &title, const QString &text, bool warning);
    void setStage(Stage s);

    UpdateChecker *m_checker;
    UpdateDownloader *m_downloader;
    QPointer<QWidget> m_parent;
    QPointer<QProgressDialog> m_progress;
    QProcess *m_installer = nullptr;
    std::unique_ptr<QTemporaryDir> m_tmp;
    std::unique_ptr<QTemporaryDir> m_installDir;
    ReleaseInfo m_release;
    QString m_pkgPath;
    QString m_currentVersion;
    QString m_lastMessage;
    Stage m_stage = Stage::Idle;
    bool m_manual = false;
    bool m_startupScheduled = false;
};

} // namespace ztodo
