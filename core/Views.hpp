#pragma once

#include "Store.hpp"

#include <QDate>
#include <QList>
#include <QString>

namespace ztodo {

enum class ViewKind { Inbox, Today, Upcoming, Completed, Project, Label, Search };

struct View {
    ViewKind kind = ViewKind::Today;
    QString id;   // project / label id
    QString text; // search text

    bool operator==(const View &o) const { return kind == o.kind && id == o.id && text == o.text; }
    // Inbox and projects keep their manual order and show subtasks nested.
    bool isManualOrder() const { return kind == ViewKind::Inbox || kind == ViewKind::Project; }
};

struct Row {
    bool header = false;
    QString headerText;
    bool overdueHeader = false;
    Task task;
    int depth = 0;
};

// The rows a view shows: section headers and tasks (with subtask depth).
//   Inbox / Project: open tasks in manual order, subtasks nested under parents
//   Today:           "Overdue" then "Today", by priority, time, order
//   Upcoming:        one section per date after today
//   Label:           open tasks with the label
//   Completed:       done tasks by completion time, newest first, per day
//   Search:          title or notes contain the text (open first)
QList<Row> buildRows(const QList<Task> &tasks, const View &view, const QDate &today);

// Number of open tasks a view shows (sidebar badges).
int countFor(const QList<Task> &tasks, const View &view, const QDate &today);

} // namespace ztodo
