#pragma once

#include "Settings.hpp"
#include "Store.hpp"
#include "Views.hpp"

#include <QJsonObject>
#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
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
    bool canUndo() const { return !m_undo.isEmpty(); }
    void runSyncCommand();

protected:
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    struct Change {
        QString kind;
        QString id;
        QJsonObject before;
    };
    struct UndoEntry {
        QString text;
        QList<Change> changes;
    };

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

    Store *m_store;
    View m_view;
    QListWidget *m_nav;
    QListWidget *m_list;
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
    QList<UndoEntry> m_undo;
    QDateTime m_lastSyncRun;
    bool m_refreshing = false;
};

} // namespace ztodo
