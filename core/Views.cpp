#include "Views.hpp"

#include <QHash>
#include <QLocale>
#include <QSet>

#include <algorithm>
#include <functional>

namespace ztodo {

namespace {

bool byPriority(const Task &a, const Task &b)
{
    if (a.priority != b.priority) {
        return a.priority < b.priority;
    }
    if (a.dueTime.isValid() != b.dueTime.isValid()) {
        return a.dueTime.isValid(); // timed tasks first
    }
    if (a.dueTime != b.dueTime) {
        return a.dueTime < b.dueTime;
    }
    return a.order < b.order;
}

bool byDue(const Task &a, const Task &b)
{
    if (a.dueDate != b.dueDate) {
        return a.dueDate < b.dueDate;
    }
    return byPriority(a, b);
}

Row header(const QString &text, bool overdue = false)
{
    Row r;
    r.header = true;
    r.headerText = text;
    r.overdueHeader = overdue;
    return r;
}

Row taskRow(const Task &t, int depth = 0)
{
    Row r;
    r.task = t;
    r.depth = depth;
    return r;
}

// Tasks nested under their parents; a subtask whose parent isn't in `set`
// is shown at the top level.
QList<Row> nested(const QList<Task> &set)
{
    QHash<QString, QList<Task>> children;
    QSet<QString> ids;
    for (const Task &t : set) {
        ids.insert(t.id);
    }
    QList<Task> roots;
    for (const Task &t : set) {
        if (!t.parent.isEmpty() && ids.contains(t.parent) && t.parent != t.id) {
            children[t.parent] << t;
        } else {
            roots << t;
        }
    }
    QList<Row> rows;
    QSet<QString> seen; // guards against parent cycles from bad data
    std::function<void(const Task &, int)> add = [&](const Task &t, int depth) {
        if (seen.contains(t.id)) {
            return;
        }
        seen.insert(t.id);
        rows << taskRow(t, depth);
        for (const Task &c : children.value(t.id)) {
            add(c, std::min(depth + 1, 4));
        }
    };
    for (const Task &t : roots) {
        add(t, 0);
    }
    return rows;
}

QString dayHeader(const QDate &d, const QDate &today)
{
    const QLocale c(QLocale::English);
    QString s = c.toString(d, QStringLiteral("ddd MMM d"));
    if (d.year() != today.year()) {
        s += c.toString(d, QStringLiteral(" yyyy"));
    }
    const qint64 delta = today.daysTo(d);
    if (delta == 0) {
        s += QStringLiteral(" · Today");
    } else if (delta == 1) {
        s += QStringLiteral(" · Tomorrow");
    } else if (delta == -1) {
        s += QStringLiteral(" · Yesterday");
    }
    return s;
}

bool matches(const Task &t, const View &v, const QDate &today)
{
    switch (v.kind) {
    case ViewKind::Inbox:
        return !t.done && t.project.isEmpty();
    case ViewKind::Project:
        return !t.done && t.project == v.id;
    case ViewKind::Today:
        return !t.done && t.dueDate.isValid() && t.dueDate <= today;
    case ViewKind::Upcoming:
        return !t.done && t.dueDate.isValid() && t.dueDate > today;
    case ViewKind::Label:
        return !t.done && t.labels.contains(v.id);
    case ViewKind::Completed:
        return t.done;
    case ViewKind::Search: {
        const QString q = v.text.trimmed();
        return !q.isEmpty()
            && (t.title.contains(q, Qt::CaseInsensitive) || t.notes.contains(q, Qt::CaseInsensitive));
    }
    }
    return false;
}

} // namespace

QList<Row> buildRows(const QList<Task> &all, const View &view, const QDate &today)
{
    QList<Task> set;
    for (const Task &t : all) {
        if (matches(t, view, today)) {
            set << t;
        }
    }
    QList<Row> rows;
    switch (view.kind) {
    case ViewKind::Inbox:
    case ViewKind::Project:
        return nested(set); // `all` is in manual order already
    case ViewKind::Today: {
        QList<Task> overdue;
        QList<Task> due;
        for (const Task &t : set) {
            (t.dueDate < today ? overdue : due) << t;
        }
        std::sort(overdue.begin(), overdue.end(), byDue);
        std::sort(due.begin(), due.end(), byPriority);
        if (!overdue.isEmpty()) {
            rows << header(QStringLiteral("Overdue"), true);
            for (const Task &t : overdue) {
                rows << taskRow(t);
            }
        }
        if (!overdue.isEmpty() || !due.isEmpty()) {
            rows << header(dayHeader(today, today));
        }
        for (const Task &t : due) {
            rows << taskRow(t);
        }
        return rows;
    }
    case ViewKind::Upcoming: {
        std::sort(set.begin(), set.end(), byDue);
        QDate current;
        for (const Task &t : set) {
            if (t.dueDate != current) {
                current = t.dueDate;
                rows << header(dayHeader(current, today));
            }
            rows << taskRow(t);
        }
        return rows;
    }
    case ViewKind::Label: {
        std::sort(set.begin(), set.end(), [](const Task &a, const Task &b) {
            if (a.dueDate.isValid() != b.dueDate.isValid()) {
                return a.dueDate.isValid();
            }
            return byDue(a, b);
        });
        for (const Task &t : set) {
            rows << taskRow(t);
        }
        return rows;
    }
    case ViewKind::Completed: {
        std::sort(set.begin(), set.end(), [](const Task &a, const Task &b) { return a.doneAt > b.doneAt; });
        QDate current;
        int n = 0;
        for (const Task &t : set) {
            if (++n > 500) {
                break;
            }
            const QDate d = t.doneAt.toLocalTime().date();
            if (d != current || n == 1) {
                current = d;
                rows << header(d.isValid() ? dayHeader(d, today) : QStringLiteral("Completed"));
            }
            rows << taskRow(t);
        }
        return rows;
    }
    case ViewKind::Search: {
        std::stable_sort(set.begin(), set.end(), [](const Task &a, const Task &b) { return !a.done && b.done; });
        for (const Task &t : set) {
            rows << taskRow(t);
        }
        return rows;
    }
    }
    return rows;
}

int countFor(const QList<Task> &tasks, const View &view, const QDate &today)
{
    if (view.kind == ViewKind::Completed || view.kind == ViewKind::Search) {
        return 0;
    }
    int n = 0;
    for (const Task &t : tasks) {
        n += matches(t, view, today) ? 1 : 0;
    }
    return n;
}

} // namespace ztodo
