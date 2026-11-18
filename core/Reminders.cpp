#include "Reminders.hpp"

#include <algorithm>

namespace ztodo {

QList<Reminder> ReminderScheduler::poll(const QList<Task> &tasks, const QDateTime &now)
{
    // Forget old entries so the set stays small in a long-running session.
    for (auto it = m_fired.begin(); it != m_fired.end();) {
        it = it.value().addDays(1) < now ? m_fired.erase(it) : std::next(it);
    }
    QList<Reminder> out;
    for (const Task &t : tasks) {
        if (t.done || !t.dueDate.isValid() || !t.dueTime.isValid()) {
            continue;
        }
        const QDateTime due(t.dueDate, t.dueTime);
        const QDateTime fireAt = due.addSecs(-qint64(m_lead) * 60);
        if (fireAt > now || due.addSecs(qint64(m_grace) * 60) < now) {
            continue; // not yet, or too late to be useful
        }
        const QString key = t.id + QLatin1Char('|') + t.dueString();
        if (m_fired.contains(key)) {
            continue;
        }
        m_fired.insert(key, due);
        out << Reminder{t.id, t.title, due};
    }
    std::sort(out.begin(), out.end(), [](const Reminder &a, const Reminder &b) { return a.due < b.due; });
    return out;
}

} // namespace ztodo
