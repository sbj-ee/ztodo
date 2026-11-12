#include "SettingsDialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace ztodo {

SettingsDialog::SettingsDialog(const AppSettings &current, bool welcome, QWidget *parent)
    : QDialog(parent)
    , m_settings(current)
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(welcome ? QStringLiteral("Welcome to ztodo") : QStringLiteral("Preferences"));
    auto *layout = new QVBoxLayout(this);

    QString intro = QStringLiteral(
        "<p>ztodo keeps your tasks in a folder that Box syncs to all your devices. Pick the "
        "same folder (inside Box) on every Mac and Linux machine.</p>");
    const QStringList roots = boxRoots();
    if (roots.isEmpty()) {
        intro += QStringLiteral("<p><b>No Box folder was found.</b> On macOS install Box Drive; on Linux sync "
                                "<tt>~/Box</tt> with rclone (see the README). Any folder works, but only a "
                                "synced one is shared.</p>");
    } else {
        intro += QStringLiteral("<p>Box folder found: <tt>%1</tt></p>").arg(roots.join(QStringLiteral(", ")).toHtmlEscaped());
    }
    auto *label = new QLabel(intro);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *form = new QFormLayout;
    m_dir = new QLineEdit(current.dataDir.isEmpty() ? suggestedDataDir() : current.dataDir);
    m_dir->setObjectName(QStringLiteral("dataDir"));
    auto *browse = new QPushButton(QStringLiteral("&Browse…"));
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Data Folder"), m_dir->text());
        if (!d.isEmpty()) {
            m_dir->setText(d);
        }
    });
    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(browse);
    form->addRow(QStringLiteral("&Data folder:"), dirRow);

    m_sync = new QLineEdit(welcome && current.syncCommand.isEmpty() ? suggestedSyncCommand() : current.syncCommand);
    m_sync->setObjectName(QStringLiteral("syncCommand"));
    m_sync->setPlaceholderText(QStringLiteral("optional, e.g. systemctl --user start --no-block boxsync.service"));
    form->addRow(QStringLiteral("&Sync command:"), m_sync);
    auto *syncHelp = new QLabel(QStringLiteral(
        "<small>Run a few seconds after each change and on File → Sync Now, so other devices see changes "
        "sooner. Leave empty with Box Drive (it syncs by itself).</small>"));
    syncHelp->setWordWrap(true);
    form->addRow(QString(), syncHelp);

    m_updates = new QCheckBox(QStringLiteral("Check for &updates at startup (at most once a day)"));
    m_updates->setObjectName(QStringLiteral("checkUpdates"));
    m_updates->setChecked(current.checkForUpdatesOnStartup);
    form->addRow(QString(), m_updates);

    auto *device = new QLabel(QStringLiteral("<small>This device: <tt>%1</tt></small>").arg(deviceId().toHtmlEscaped()));
    device->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QString(), device);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    if (welcome) {
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("&Start"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("&Quit"));
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_dir, &QLineEdit::textChanged, this, [buttons](const QString &t) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!t.trimmed().isEmpty());
    });
    layout->addWidget(buttons);
    resize(620, 0);
}

AppSettings SettingsDialog::result() const
{
    AppSettings s = m_settings;
    s.dataDir = QDir::cleanPath(QDir::fromNativeSeparators(m_dir->text().trimmed()));
    if (s.dataDir.startsWith(QLatin1String("~/"))) {
        s.dataDir = QDir::homePath() + s.dataDir.mid(1);
    }
    s.syncCommand = m_sync->text().trimmed();
    s.checkForUpdatesOnStartup = m_updates->isChecked();
    return s;
}

} // namespace ztodo
