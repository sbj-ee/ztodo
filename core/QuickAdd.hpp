#pragma once

#include <QDate>
#include <QString>
#include <QStringList>
#include <QTime>

namespace ztodo {

// Todoist-style quick add:
//   "Pay rent every month 1st p1 #Home @bills"
//   "Call mom tomorrow at 5pm"
//   "Standup every weekday 9:30am #Work"
// Recognised (and removed from the title): a recurrence (Recurrence.hpp), a
// date and a time (DateText.hpp), p1..p4, #project and @label (one word; an
// underscore stands for a space: #Home_Improvement).
struct QuickAdd {
    QString title;
    QDate due;
    QTime time;
    QString recur;
    int priority = 4;
    QString project;    // name as typed, without '#'
    QStringList labels; // names, without '@'

    static QuickAdd parse(const QString &text, const QDate &today = QDate::currentDate());
};

} // namespace ztodo
