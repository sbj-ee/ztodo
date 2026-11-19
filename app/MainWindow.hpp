#pragma once

#include "Reminders.hpp"
#include "Settings.hpp"
#include "Store.hpp"
#include "UndoStack.hpp"
#include "Views.hpp"

#include <QJsonObject>
#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSystemTrayIcon;
class QTimer;

namespace ztodo {

class TaskDelegate;

// Sidebar (Inbox, Today, Upcoming, Completed, projects, labels) | header,
// quick-add line and the task list of the selected view.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Store *store, QWidget *parent = nullptr);

    void setView(const View &v);
    View view() const { return m_view; }
    QListWidget *taskList() const { return m_list; }
    QListWidget *nav() const { return m_nav; }
    QLineEdit *quickAdd() const { return m_quick; }

    // Adds a task from quick-add text (with the current view's defaults).
    QString addFromText(const QString &text);
    void toggleComplete(const QString &taskId);
    void undo();
    bool canUndo() const { return m_undo.canUndo(); }
    void runSyncCommand();
    // "ztodo <version> — <view>" (just "ztodo <version>" without a view).
    static QString windowTitleFor(const QString &viewTitle);

protected:
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    using Change = UndoStack::Change;

    void buildMenus();
    void refresh();
    void refreshNav();
    void refreshList();
    void updateQuickPreview();
    void selectNav(const View &v);
    QString currentTaskId() const;
    void editTask(const QString &id);
    void addSubtask(const QString &parentId);
    void deleteTask(const QString &id);
    void moveTask(const QString &id, int delta);
    void setTaskField(const QString &id, const QString &field, const QJsonValue &value, const QString &undoText);
    void taskContextMenu(const QPoint &pos);
    void navContextMenu(const QPoint &pos);
    void addProject();
    void renameEntity(const QString &kind, const QString &id);
    void deleteProject(const QString &id);
    void deleteLabel(const QString &id);
    void pushUndo(const QString &text, const QList<Change> &changes);
    void notify(const QString &text, bool offerUndo);
    void openPreferences();
    void about();
    void scheduleSync();
    void updateStatus();
    void applyTheme(); // sidebar and list colours that depend on the light/dark palette
    void applyFont();  // view title, quick-add height, and list row sizes for the app font
    void checkReminders();

    Store *m_store;
    View m_view;
    QListWidget *m_nav = nullptr; // null until the constructor builds it (changeEvent can fire earlier)
    QListWidget *m_list = nullptr;
    QLineEdit *m_quick;
    QLabel *m_quickPreview;
    QLabel *m_title;
    QLineEdit *m_search;
    QLabel *m_empty;
    QLabel *m_status;
    QPushButton *m_undoButton;
    TaskDelegate *m_delegate;
    QTimer *m_syncTimer;
    QTimer *m_notifyTimer;
    QTimer *m_midnight;
    QTimer *m_remindTimer;
    QSystemTrayIcon *m_tray = nullptr;
    ReminderScheduler m_reminders;
    UndoStack m_undo;
    QDateTime m_lastSyncRun;
    bool m_refreshing = false;
    bool m_themePending = false;
};

} // namespace ztodo
