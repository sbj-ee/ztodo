#include "SettingsDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

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
    auto *dirLabel = new QLabel(QStringLiteral("&Data folder:"));
    dirLabel->setObjectName(QStringLiteral("dataDirLabel"));
    dirLabel->setBuddy(m_dir);
    form->addRow(dirLabel, dirRow);

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

    m_appearance = new QComboBox;
    m_appearance->setObjectName(QStringLiteral("appearance"));
    m_appearance->addItem(QStringLiteral("Follow the system"), QStringLiteral("system"));
    m_appearance->addItem(QStringLiteral("Light"), QStringLiteral("light"));
    m_appearance->addItem(QStringLiteral("Dark"), QStringLiteral("dark"));
    m_appearance->setCurrentIndex(std::max(0, m_appearance->findData(current.appearance)));
    form->addRow(QStringLiteral("&Appearance:"), m_appearance);

    m_font = new QFontComboBox;
    m_font->setObjectName(QStringLiteral("fontFamily"));
    m_font->insertItem(0, QStringLiteral("System default"));
    if (current.fontFamily.isEmpty()) {
        m_font->setCurrentIndex(0);
    } else {
        const int i = m_font->findText(current.fontFamily);
        if (i > 0) {
            m_font->setCurrentIndex(i);
        } else {
            m_font->addItem(current.fontFamily);
            m_font->setCurrentIndex(m_font->count() - 1);
        }
    }
    auto *fontLabel = new QLabel(QStringLiteral("&Font:"));
    fontLabel->setObjectName(QStringLiteral("fontFamilyLabel"));
    fontLabel->setBuddy(m_font);
    form->addRow(fontLabel, m_font);

    m_fontSize = new QComboBox;
    m_fontSize->setObjectName(QStringLiteral("fontSize"));
    m_fontSize->addItem(QStringLiteral("Small"), QStringLiteral("small"));
    m_fontSize->addItem(QStringLiteral("Medium"), QStringLiteral("medium"));
    m_fontSize->addItem(QStringLiteral("Large"), QStringLiteral("large"));
    m_fontSize->setCurrentIndex(std::max(0, m_fontSize->findData(current.fontSize)));
    auto *fontSizeLabel = new QLabel(QStringLiteral("&Size:"));
    fontSizeLabel->setObjectName(QStringLiteral("fontSizeLabel"));
    fontSizeLabel->setBuddy(m_fontSize);
    form->addRow(fontSizeLabel, m_fontSize);

    m_remind = new QCheckBox(QStringLiteral("Show a &reminder when a task with a time is due"));
    m_remind->setObjectName(QStringLiteral("remindersEnabled"));
    m_remind->setChecked(current.remindersEnabled);
    form->addRow(QString(), m_remind);
    m_lead = new QComboBox;
    m_lead->setObjectName(QStringLiteral("reminderLead"));
    for (int m : {0, 5, 10, 15, 30, 60}) {
        m_lead->addItem(m == 0 ? QStringLiteral("At the due time") : QStringLiteral("%1 minutes before").arg(m), m);
    }
    if (const int i = m_lead->findData(current.reminderLeadMinutes); i >= 0) {
        m_lead->setCurrentIndex(i);
    } else {
        m_lead->addItem(QStringLiteral("%1 minutes before").arg(current.reminderLeadMinutes), current.reminderLeadMinutes);
        m_lead->setCurrentIndex(m_lead->count() - 1);
    }
    m_lead->setEnabled(m_remind->isChecked());
    connect(m_remind, &QCheckBox::toggled, m_lead, &QWidget::setEnabled);
    form->addRow(QStringLiteral("Remind me:"), m_lead);

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
    s.appearance = m_appearance->currentData().toString();
    s.fontFamily = (m_font->currentIndex() == 0) ? QString() : m_font->currentText();
    s.fontSize = m_fontSize->currentData().toString();
    s.remindersEnabled = m_remind->isChecked();
    s.reminderLeadMinutes = m_lead->currentData().toInt();
    return s;
}

} // namespace ztodo
