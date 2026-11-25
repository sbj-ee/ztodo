#pragma once

#include <QDate>
#include <QList>
#include <QString>

namespace ztodo {

// Recurring due dates, a subset of Todoist's syntax:
//   every day | daily | every 3 days | every other day
//   every week | weekly | every 2 weeks | every other week
//   every month | monthly | every 3 months | every 15th
//   every year | yearly | annually
//   every weekday | every workday | every weekend
//   every mon | every mon, wed and fri | every tuesday
// "every!" (with the bang) counts from the completion date instead of the due
// date: "every! 3 days" = three days after you last did it.
struct Recurrence {
    enum class Unit { Day, Week, Month, Year, Weekdays };
    bool valid = false;
    bool fromCompletion = false;
    Unit unit = Unit::Day;
    int interval = 1;
    QList<int> weekdays; // Unit::Weekdays: 1 (Mon) .. 7 (Sun), sorted
    int monthDay = 0;    // Unit::Month: fixed day of month (0 = same day as base)

    static Recurrence parse(const QString &text);
    // The next occurrence strictly after `base`. For month and year rules
    // (other than "every 15th"), `anchorDay` is the day of the month the task
    // really falls on: a task on the 31st moved to Feb 28 goes back to the
    // 31st (Mar 31), instead of staying on the 28th. 0 = base's own day.
    QDate next(const QDate &base, int anchorDay = 0) const;
    // First occurrence on or after `today` (a new recurring task with no date).
    QDate first(const QDate &today) const;
    // Where a completed task moves: after its due date (or after today for
    // "every!"), and always after today. `anchorDay` as for next().
    QDate afterCompletion(const QDate &due, const QDate &today, int anchorDay = 0) const;

    // The day of the month a task due on `due` repeats on, given the anchor
    // stored with it. The anchor counts only when `due` is the last day of
    // its month and the anchor is later (the date was cut short by a short
    // month); otherwise it is `due`'s own day, so a stale anchor (the date was
    // changed by a version that doesn't keep one) can't pull it elsewhere.
    static int anchorFor(const QDate &due, int anchorDay);
};

// The regex matching a recurrence phrase inside free text (quick add).
QString recurrencePattern();

} // namespace ztodo
