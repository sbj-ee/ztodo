#pragma once

#include <QStyledItemDelegate>

namespace ztodo {

class Store;

// Item data roles shared by the task list and the sidebar.
enum Role {
    RoleIsHeader = Qt::UserRole + 1,
    RoleTaskId,
    RoleDepth,
    RoleOverdueHeader,
    RoleNavKind, // int(ViewKind) or -1 for a section header, -2 for an "add" row
    RoleNavId,
    RoleCount,
    RoleColor,
};

// One task row: priority-coloured completion circle, title, then a line with
// the due date (coloured by urgency), recurrence, labels and project.
// Clicking the circle emits toggleRequested(id).
class TaskDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    TaskDelegate(Store *store, QObject *parent = nullptr);
    void setShowProject(bool on) { m_showProject = on; }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &opt,
                     const QModelIndex &index) override;

    static QRect circleRect(const QRect &row, int depth);

signals:
    void toggleRequested(const QString &taskId);

private:
    Store *m_store;
    bool m_showProject = true;
};

// Sidebar rows: built-in views with drawn icons, section headers, projects
// (colour dot) and labels, with open-task counts on the right.
class NavDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
};

} // namespace ztodo
