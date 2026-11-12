#include "UpdateManager.hpp"

#include "Settings.hpp"
#include "UpdateDialog.hpp"
#include "version.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>
#include <QUrl>

namespace ztodo {

namespace {
const QString kInstalledBinary = QStringLiteral("/usr/bin/ztodo");
// Absolute paths: never whatever "pkexec"/"apt" $PATH finds first.
const QString kPkexec = QStringLiteral("/usr/bin/pkexec");
const QString kApt = QStringLiteral("/usr/bin/apt");
} // namespace

UpdateManager &UpdateManager::instance()
{
    static UpdateManager *m = new UpdateManager(qApp);
    return *m;
}

UpdateManager::UpdateManager(QObject *parent)
    : QObject(parent)
    , m_checker(new UpdateChecker(this))
    , m_downloader(new UpdateDownloader(this))
    , m_currentVersion(QString::fromLatin1(kVersionString))
{
    connect(m_checker, &UpdateChecker::finished, this, &UpdateManager::onCheckFinished);
    connect(m_downloader, &UpdateDownloader::finished, this, &UpdateManager::onDownloadFinished);
    connect(m_downloader, &UpdateDownloader::progress, this, [this](const QString &name, qint64 got, qint64 total) {
        if (!m_progress) {
            return;
        }
        m_progress->setLabelText(QStringLiteral("Downloading %1…").arg(name));
        if (total > 0) {
            m_progress->setMaximum(1000);
            m_progress->setValue(int(got * 1000 / total));
        } else {
            m_progress->setMaximum(0);
        }
    });
}

void UpdateManager::setStage(Stage s)
{
    if (m_stage != s) {
        m_stage = s;
        emit stageChanged(s);
    }
}

QStringList UpdateManager::installCommand(const QString &debPath)
{
    return {kPkexec, kApt, QStringLiteral("install"), QStringLiteral("-y"), QFileInfo(debPath).absoluteFilePath()};
}

QString UpdateManager::stageVerifiedPackage(const QString &path, const QString &sha256, QTemporaryDir &into,
                                            QString *error)
{
    auto fail = [error](const QString &why) {
        if (error) {
            *error = why;
        }
        return QString();
    };
    if (!into.isValid()) {
        return fail(QStringLiteral("Couldn't create a private folder for the package."));
    }
    QFile::setPermissions(into.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString dest = QDir(into.path()).filePath(QFileInfo(path).fileName());
    if (!QFile::copy(path, dest)) {
        return fail(QStringLiteral("Couldn't copy the package to %1.").arg(into.path()));
    }
    QFile::setPermissions(dest, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup
                                    | QFileDevice::ReadOther);
    if (sha256.isEmpty() || sha256OfFile(dest) != sha256.toLower()) {
        QFile::remove(dest);
        return fail(QStringLiteral("The package changed after it was verified, so it was not installed."));
    }
    return QFileInfo(dest).absoluteFilePath();
}

void UpdateManager::checkNow(QWidget *parent)
{
    startCheck(parent, true);
}

void UpdateManager::scheduleStartupCheck(QWidget *parent, int delayMs)
{
    if (m_startupScheduled) {
        return;
    }
    m_startupScheduled = true;
    QPointer<QWidget> p(parent);
    QTimer::singleShot(delayMs, this, [this, p]() {
        if (p) {
            startupCheckIfDue(p);
        }
    });
}

bool UpdateManager::startupCheckIfDue(QWidget *parent)
{
    if (!UpdatePolicy::shouldCheckAtStartup(AppSettings::load().checkForUpdatesOnStartup, UpdateState::load(),
                                            QDateTime::currentDateTimeUtc())) {
        return false;
    }
    startCheck(parent, false);
    return true;
}

void UpdateManager::startCheck(QWidget *parent, bool manual)
{
    if (m_stage != Stage::Idle && m_stage != Stage::Done) {
        if (manual && m_stage == Stage::Checking) {
            m_manual = true;
            m_parent = parent;
        }
        return;
    }
    m_parent = parent;
    m_manual = manual;
    setStage(Stage::Checking);
    m_checker->check();
}

void UpdateManager::onCheckFinished(const UpdateChecker::Result &r)
{
    if (!m_manual) {
        UpdateState st = UpdateState::load();
        st.lastCheck = QDateTime::currentDateTimeUtc();
        st.save();
    }
    setStage(Stage::Idle);
    if (r.outcome != UpdateChecker::Outcome::Release) {
        m_lastMessage = r.error;
        if (m_manual) {
            message(QStringLiteral("Check for Updates"), r.error, r.outcome == UpdateChecker::Outcome::Error);
        }
        emit checkFinished(false);
        return;
    }
    m_release = r.release;
    if (!UpdatePolicy::shouldOffer(m_release, m_currentVersion, UpdateState::load(), m_manual)) {
        m_lastMessage = QStringLiteral("You're running the latest version of ztodo (%1).").arg(m_currentVersion);
        if (m_manual) {
            message(QStringLiteral("Check for Updates"), m_lastMessage, false);
        }
        emit checkFinished(false);
        return;
    }
    offer();
    emit checkFinished(true);
}

void UpdateManager::offer()
{
    setStage(Stage::Offering);
    auto *dlg = new UpdateDialog(m_release, m_currentVersion, m_parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::finished, this, [this](int result) {
        if (result == UpdateDialog::Skip) {
            UpdateState st = UpdateState::load();
            st.skippedVersion = m_release.tag;
            st.save();
            setStage(Stage::Idle);
        } else if (result == UpdateDialog::Install) {
            startDownload();
        } else {
            setStage(Stage::Idle);
        }
    });
    dlg->open();
}

void UpdateManager::startDownload()
{
    m_tmp = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/ztodo-update-XXXXXX"));
    if (!m_tmp->isValid()) {
        message(QStringLiteral("Update"), QStringLiteral("Couldn't create a temporary folder for the download."), true);
        setStage(Stage::Idle);
        return;
    }
    setStage(Stage::Downloading);
    m_progress = new QProgressDialog(QStringLiteral("Downloading…"), QStringLiteral("Cancel"), 0, 0, m_parent);
    m_progress->setObjectName(QStringLiteral("updateProgress"));
    m_progress->setWindowTitle(QStringLiteral("Updating ztodo"));
    m_progress->setAttribute(Qt::WA_DeleteOnClose);
    m_progress->setAutoClose(false);
    m_progress->setAutoReset(false);
    m_progress->setMinimumDuration(0);
    connect(m_progress, &QProgressDialog::canceled, m_downloader, &UpdateDownloader::cancel);
    m_progress->open();
    m_downloader->start(m_release, m_tmp->path());
}

void UpdateManager::onDownloadFinished(bool ok, const QString &pkgPath, const QString &error)
{
    if (m_progress) {
        m_progress->disconnect(m_downloader);
        m_progress->close();
    }
    if (!ok) {
        m_tmp.reset();
        setStage(Stage::Idle);
        m_lastMessage = error;
        if (error != QLatin1String("Download cancelled.")) {
            message(QStringLiteral("Update Not Installed"), error, true);
        }
        emit installFinished(false, error);
        return;
    }
    m_pkgPath = pkgPath;
    runInstaller();
}

#if defined(Q_OS_MACOS)
QString UpdateManager::runningBundle()
{
    // .../ztodo.app/Contents/MacOS/ztodo
    QDir d(QCoreApplication::applicationDirPath());
    if (!d.cdUp() || !d.cdUp() || !d.absolutePath().endsWith(QLatin1String(".app"))) {
        return {};
    }
    return d.absolutePath();
}

QString UpdateManager::replaceBundleFromDmg(const QString &dmg, const QString &bundle, const QString &workDir)
{
    auto run = [](const QString &prog, const QStringList &args, int timeoutMs, QString *out) {
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(prog, args);
        if (!p.waitForFinished(timeoutMs)) {
            p.kill();
            p.waitForFinished(2000);
            *out = QStringLiteral("%1 timed out").arg(prog);
            return false;
        }
        *out = QString::fromLocal8Bit(p.readAll()).trimmed();
        return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    };
    const QString mnt = workDir + QStringLiteral("/mnt");
    QDir().mkpath(mnt);
    QString out;
    if (!run(QStringLiteral("/usr/bin/hdiutil"),
             {QStringLiteral("attach"), QStringLiteral("-nobrowse"), QStringLiteral("-readonly"),
              QStringLiteral("-noautoopen"), QStringLiteral("-mountpoint"), mnt, dmg},
             120000, &out)) {
        return QStringLiteral("Couldn't open the disk image: %1").arg(out);
    }
    QString err;
    const QString src = mnt + QStringLiteral("/ztodo.app");
    const QString staged = bundle + QStringLiteral(".new");
    const QString old = bundle + QStringLiteral(".old");
    if (!QFileInfo(src).isDir()) {
        err = QStringLiteral("The disk image has no ztodo.app.");
    } else {
        QDir(staged).removeRecursively();
        QDir(old).removeRecursively();
        // ditto keeps the bundle's symlinks, permissions and code signature.
        if (!run(QStringLiteral("/usr/bin/ditto"), {src, staged}, 300000, &out)) {
            err = QStringLiteral("Couldn't copy ztodo.app next to %1: %2").arg(bundle, out);
        } else if (!QFile::rename(bundle, old)) {
            err = QStringLiteral("Couldn't move the old %1 aside (no permission?).").arg(bundle);
        } else if (!QFile::rename(staged, bundle)) {
            QFile::rename(old, bundle); // put the old one back
            err = QStringLiteral("Couldn't put the new ztodo.app in place of %1.").arg(bundle);
        } else {
            QDir(old).removeRecursively();
        }
        QDir(staged).removeRecursively();
    }
    run(QStringLiteral("/usr/bin/hdiutil"), {QStringLiteral("detach"), mnt, QStringLiteral("-quiet")}, 60000, &out);
    return err;
}
#endif

void UpdateManager::runInstaller()
{
    m_installDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/ztodo-install-XXXXXX"));
    QString stageError;
    const QString staged = stageVerifiedPackage(m_pkgPath, m_downloader->verifiedSha256(), *m_installDir, &stageError);
    if (staged.isEmpty()) {
        m_installDir.reset();
        m_tmp.reset();
        setStage(Stage::Idle);
        message(QStringLiteral("Update Not Installed"), stageError, true);
        emit installFinished(false, stageError);
        return;
    }
    setStage(Stage::Installing);
#if defined(Q_OS_MACOS)
    const QString bundle = runningBundle();
    if (bundle.isEmpty()) {
        fallback(QStringLiteral("This ztodo isn't running from an app bundle, so it can't replace itself."));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QString err = replaceBundleFromDmg(staged, bundle, m_installDir->path());
    QApplication::restoreOverrideCursor();
    if (!err.isEmpty()) {
        fallback(err);
        return;
    }
    installed();
#else
    if (QCoreApplication::applicationFilePath() != kInstalledBinary) {
        fallback(QStringLiteral("This ztodo (%1) wasn't installed from the .deb, so the package can't be "
                                "installed in its place automatically.")
                     .arg(QCoreApplication::applicationFilePath()));
        return;
    }
    const QStringList cmd = installCommand(staged);
    if (!QFileInfo(cmd.at(0)).isExecutable() || !QFileInfo(cmd.at(1)).isExecutable()) {
        fallback(QStringLiteral("pkexec or apt isn't available, so the package can't be installed from here."));
        return;
    }
    m_progress = new QProgressDialog(QStringLiteral("Installing %1…\nEnter your password in the system prompt.")
                                         .arg(QFileInfo(staged).fileName()),
                                     QString(), 0, 0, m_parent);
    m_progress->setObjectName(QStringLiteral("updateProgress"));
    m_progress->setWindowTitle(QStringLiteral("Updating ztodo"));
    m_progress->setAttribute(Qt::WA_DeleteOnClose);
    m_progress->setMinimumDuration(0);
    m_progress->setCancelButton(nullptr);
    m_progress->open();

    m_installer = new QProcess(this);
    m_installer->setWorkingDirectory(m_installDir->path());
    m_installer->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_installer, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus st) { onInstallerFinished(code, st == QProcess::CrashExit); });
    connect(m_installer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            onInstallerFinished(127, false);
        }
    });
    m_installer->start(cmd.at(0), cmd.mid(1)); // argv list, no shell
#endif
}

void UpdateManager::onInstallerFinished(int exitCode, bool crashed)
{
    if (!m_installer) {
        return;
    }
    const QString output = QString::fromLocal8Bit(m_installer->readAll()).trimmed();
    m_installer->deleteLater();
    m_installer = nullptr;
    if (m_progress) {
        m_progress->close();
    }
    if (crashed || exitCode != 0) {
        QString why;
        if (exitCode == 126) {
            why = QStringLiteral("The password prompt was dismissed, so nothing was installed.");
        } else if (exitCode == 127) {
            why = QStringLiteral("Not authorised to install packages (pkexec exit 127).");
        } else {
            why = QStringLiteral("apt couldn't install the package (exit %1).").arg(exitCode);
            if (!output.isEmpty()) {
                why += QLatin1Char('\n') + output.right(600);
            }
        }
        fallback(why);
        return;
    }
    installed();
}

void UpdateManager::installed()
{
    setStage(Stage::Done);
    m_lastMessage = QStringLiteral("ztodo %1 was installed.").arg(m_release.tag);
    m_tmp.reset();
    m_installDir.reset();
    emit installFinished(true, m_lastMessage);
    auto *box = new QMessageBox(QMessageBox::Question, QStringLiteral("Restart ztodo"),
                                QStringLiteral("ztodo %1 was installed. Restart now to use it?").arg(m_release.tag),
                                QMessageBox::NoButton, m_parent);
    box->setObjectName(QStringLiteral("updateRestart"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton *restartBtn = box->addButton(QStringLiteral("&Restart Now"), QMessageBox::AcceptRole);
    box->addButton(QStringLiteral("&Later"), QMessageBox::RejectRole);
    box->setDefaultButton(restartBtn);
    connect(box, &QMessageBox::buttonClicked, this, [this, restartBtn](QAbstractButton *b) {
        if (b == restartBtn) {
            restart();
        }
    });
    box->open();
}

void UpdateManager::restart()
{
    // Every change is already on disk (each edit is appended immediately).
#if defined(Q_OS_MACOS)
    const QString bundle = runningBundle();
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-n"), bundle});
#else
    QProcess::startDetached(kInstalledBinary, {});
#endif
    QCoreApplication::exit(0);
}

void UpdateManager::fallback(const QString &why)
{
    setStage(Stage::Idle);
    if (m_tmp) {
        m_tmp->setAutoRemove(false); // keep the verified package for a manual install
    }
#if defined(Q_OS_MACOS)
    const QString text = QStringLiteral("%1\n\nThe verified package is at:\n%2\n\nOpen it and drag ztodo.app to "
                                        "Applications.\n\nOpening the release page.")
                             .arg(why, m_pkgPath);
#else
    const QString text = QStringLiteral("%1\n\nThe verified package is at:\n%2\n\nInstall it with:\nsudo apt install "
                                        "%2\n\nOpening the release page.")
                             .arg(why, m_pkgPath);
#endif
    m_lastMessage = text;
    if (isTrustedReleasePageUrl(m_release.htmlUrl)) {
        QDesktopServices::openUrl(QUrl(m_release.htmlUrl));
    }
    message(QStringLiteral("Update Not Installed"), text, true);
    emit installFinished(false, text);
}

void UpdateManager::message(const QString &title, const QString &text, bool warning)
{
    m_lastMessage = text;
    auto *box = new QMessageBox(warning ? QMessageBox::Warning : QMessageBox::Information, title, text,
                                QMessageBox::Ok, m_parent);
    box->setObjectName(QStringLiteral("updateMessage"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setTextInteractionFlags(Qt::TextSelectableByMouse);
    box->open();
}

} // namespace ztodo
