#pragma once

#include "Store.hpp"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

namespace ztodo {

struct Reminder {
    QString taskId;
    QString title;
    QDateTime due;
};

// Decides which tasks to remind about. Only tasks with a due *time* are
// reminded (an all-day task has no moment to fire at). A reminder fires once
// per (task, due date and time): rescheduling a task or completing a
// recurring one gives a new due moment, so it fires again. Reminders that
// are already more than `grace` late (the app was closed) are skipped rather
// than fired in a burst at startup. No widgets: the caller shows them.
class ReminderScheduler {
public:
    explicit ReminderScheduler(int leadMinutes = 0, int graceMinutes = 10)
        : m_lead(leadMinutes), m_grace(graceMinutes) {}

    void setLeadMinutes(int m) { m_lead = m < 0 ? 0 : m; }
    int leadMinutes() const { return m_lead; }

    // Reminders that became due since the last call (each at most once).
    QList<Reminder> poll(const QList<Task> &tasks, const QDateTime &now);

private:
    QHash<QString, QDateTime> m_fired; // "<id>|<due>" -> due
    int m_lead;
    int m_grace;
};

} // namespace ztodo
