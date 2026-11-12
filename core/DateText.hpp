#pragma once

#include <QDate>
#include <QString>
#include <QTime>

namespace ztodo {

// Natural-language dates and times, Todoist style. All matching is
// case-insensitive and on whole words.
//
// Dates:  today, tod, tomorrow, tmr, yesterday, mon..sun / monday..sunday
//         (on or after today), next mon, next week (Monday), next month (1st),
//         in 3 days / in 2 weeks / in 1 month, eom, nov 20, 20 nov, nov 20 2027,
//         11/20, 11/20/2027, 2026-11-20. A month-day without a year that has
//         already passed means next year.
// Times:  5pm, 5:30pm, at 17:00, at 9, noon, midnight.
struct TextMatch {
    qsizetype start = -1;
    qsizetype length = 0;
    QDate date;
    QTime time;
    bool found() const { return start >= 0; }
};

TextMatch findDate(const QString &text, const QDate &today);
TextMatch findTime(const QString &text);

// The whole of `text` is a due date (and optional time), e.g. in the task
// editor. Empty text clears (returns true with invalid date). False if any
// word is left over.
bool parseDue(const QString &text, const QDate &today, QDate *date, QTime *time);

// "Today", "Tomorrow", "Yesterday", "Friday" (within a week), "Nov 20",
// "Nov 20 2027"; plus " 5:00 PM" with a time.
QString formatDue(const QDate &date, const QTime &time, const QDate &today);

// Weekday name ("mon", "Tuesday", "thurs") -> 1..7 (Qt: Monday = 1), else 0.
int weekdayFromName(const QString &word);

// Shared regex fragment matching one weekday name (full names first so "mon"
// never matches inside "month"; callers add \b).
QString weekdayPattern();

} // namespace ztodo
