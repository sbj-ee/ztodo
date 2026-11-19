#include "Stats.hpp"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace ztodo {

QList<Completion> completionHistory(const QList<Task> &tasks, const QList<Completion> &records)
{
    // Store::complete() writes doneAt and the record's time from one string,
    // so they agree to the second.
    QSet<QString> recorded;
    for (const Completion &c : records) {
        recorded.insert(c.task + QLatin1Char('@') + QString::number(c.at.toSecsSinceEpoch()));
    }
    QList<Completion> out = records;
    for (const Task &t : tasks) {
        // A done task, or a recurring one completed at least once.
        if (!t.doneAt.isValid() || (!t.done && t.recur.isEmpty())) {
            continue;
        }
        if (!recorded.contains(t.id + QLatin1Char('@') + QString::number(t.doneAt.toSecsSinceEpoch()))) {
            out << Completion{QString(), t.id, t.project, t.doneAt};
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Completion &a, const Completion &b) { return a.at < b.at; });
    return out;
}

Stats computeStats(const QList<Task> &tasks, const QList<Completion> &history, const QDate &today, int days)
{
    Stats s;
    const QDate weekStart = today.addDays(1 - today.dayOfWeek());
    const QDate from7 = today.addDays(-6);
    const QDate from30 = today.addDays(-29);
    const QDate fromDaily = today.addDays(1 - std::max(days, 1));
    s.daily = QList<int>(std::max(days, 1), 0);

    QHash<QDate, int> perDay;
    QHash<QString, int> perProject;
    for (const Completion &c : history) {
        ++s.total;
        const QDate d = c.at.toLocalTime().date();
        if (!d.isValid() || d > today) {
            continue;
        }
        ++perDay[d];
        s.today += d == today ? 1 : 0;
        s.thisWeek += d >= weekStart ? 1 : 0;
        s.last7 += d >= from7 ? 1 : 0;
        if (d >= from30) {
            ++s.last30;
            ++perProject[c.project];
        }
        if (d >= fromDaily) {
            ++s.daily[fromDaily.daysTo(d)];
        }
    }
    s.avgPerDay30 = s.last30 / 30.0;

    for (auto it = perDay.cbegin(); it != perDay.cend(); ++it) {
        if (it.value() > s.bestDayCount || (it.value() == s.bestDayCount && it.key() > s.bestDay)) {
            s.bestDay = it.key();
            s.bestDayCount = it.value();
        }
    }

    QDate d = perDay.contains(today) ? today : today.addDays(-1);
    while (perDay.contains(d)) {
        ++s.currentStreak;
        d = d.addDays(-1);
    }
    QList<QDate> active = perDay.keys();
    std::sort(active.begin(), active.end());
    int run = 0;
    for (qsizetype i = 0; i < active.size(); ++i) {
        run = (i > 0 && active.at(i - 1).daysTo(active.at(i)) == 1) ? run + 1 : 1;
        s.longestStreak = std::max(s.longestStreak, run);
    }

    for (auto it = perProject.cbegin(); it != perProject.cend(); ++it) {
        s.byProject30 << ProjectCount{it.key(), it.value()};
    }
    std::sort(s.byProject30.begin(), s.byProject30.end(), [](const ProjectCount &a, const ProjectCount &b) {
        return a.count != b.count ? a.count > b.count : a.project < b.project;
    });

    for (const Task &t : tasks) {
        if (t.done) {
            continue;
        }
        ++s.open;
        if (t.hasDue()) {
            s.overdue += t.dueDate < today ? 1 : 0;
            s.dueToday += t.dueDate == today ? 1 : 0;
        }
    }
    return s;
}

} // namespace ztodo
