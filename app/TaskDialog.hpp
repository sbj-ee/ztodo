#pragma once

#include "Store.hpp"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace ztodo {

// Edit (or create) one task: title, notes, due date in plain words, repeat
// rule, priority, project and labels. The caller writes the result.
class TaskDialog : public QDialog {
    Q_OBJECT
public:
    enum Result { Deleted = 2 };

    TaskDialog(Store *store, const Task &task, bool isNew, QWidget *parent = nullptr);

    // The edited task (labels as ids; unknown label names are created on accept).
    Task result() const { return m_result; }
    QLineEdit *titleEdit() const { return m_title; }
    QLineEdit *dueEdit() const { return m_due; }

    // Text for the due field that parseDue() reads back to the same date.
    static QString dueEditText(const QDate &date, const QTime &time, const QDate &today);

private:
    void validate();
    void accept() override;

    Store *m_store;
    Task m_task;
    Task m_result;
    QLineEdit *m_title;
    QPlainTextEdit *m_notes;
    QLineEdit *m_due;
    QLabel *m_dueHint;
    QLineEdit *m_recur;
    QLabel *m_recurHint;
    QComboBox *m_priority;
    QComboBox *m_project;
    QLineEdit *m_labels;
    QPushButton *m_save;
};

} // namespace ztodo
