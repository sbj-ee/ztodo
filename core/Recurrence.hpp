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
    // The next occurrence strictly after `base`.
    QDate next(const QDate &base) const;
    // First occurrence on or after `today` (a new recurring task with no date).
    QDate first(const QDate &today) const;
    // Where a completed task moves: after its due date (or after today for
    // "every!"), and always after today.
    QDate afterCompletion(const QDate &due, const QDate &today) const;
};

// The regex matching a recurrence phrase inside free text (quick add).
QString recurrencePattern();

} // namespace ztodo
