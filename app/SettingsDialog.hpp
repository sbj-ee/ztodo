#pragma once

#include "Settings.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLineEdit;

namespace ztodo {

// Where the shared data lives (the Box folder), the optional sync command,
// appearance, font, reminders and update checks. With welcome = true it's
// the first-run setup.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const AppSettings &current, bool welcome, QWidget *parent = nullptr);
    AppSettings result() const;

private:
    AppSettings m_settings;
    QLineEdit *m_dir;
    QLineEdit *m_sync;
    QCheckBox *m_updates;
    QCheckBox *m_remind;
    QComboBox *m_lead;
    QComboBox *m_appearance;
    QFontComboBox *m_font;
    QComboBox *m_fontSize;
};

} // namespace ztodo
