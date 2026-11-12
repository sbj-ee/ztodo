#pragma once

#include "Settings.hpp"

#include <QDialog>

class QCheckBox;
class QLineEdit;

namespace ztodo {

// Where the shared data lives (the Box folder), the optional sync command and
// update checks. With welcome = true it's the first-run setup.
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
};

} // namespace ztodo
