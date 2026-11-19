#pragma once

#include "Store.hpp"

#include <QDate>
#include <QList>
#include <QString>

namespace ztodo {

// Productivity stats (View → Productivity): what was completed when, streaks,
// and the state of the open list. Computed locally from the Store; nothing
// leaves the machine. Days are local calendar days; weeks start on Monday.

struct ProjectCount {
    QString project; // project id; empty = Inbox
    int count = 0;
};

struct Stats {
    // Completions.
    int today = 0;
    int thisWeek = 0;  // Monday .. today
    int last7 = 0;     // today and the 6 days before
    int last30 = 0;    // today and the 29 days before
    int total = 0;     // all recorded history
    double avgPerDay30 = 0; // last30 / 30
    int currentStreak = 0;  // consecutive days with a completion, ending today
                            // (or yesterday, until today's first completion)
    int longestStreak = 0;
    QDate bestDay; // the day with the most completions (latest on a tie)
    int bestDayCount = 0;
    QList<int> daily;                 // per day, oldest first; the last entry is today
    QList<ProjectCount> byProject30;  // last 30 days, most first

    // Open tasks now.
    int open = 0;
    int overdue = 0;
    int dueToday = 0;
};

// Completion records plus, for history from before they existed (0.4.x),
// one per task whose doneAt has no matching record. A recurring task's
// earlier completions from then are lost: only its latest doneAt was kept.
QList<Completion> completionHistory(const QList<Task> &tasks, const QList<Completion> &records);

// `history` as returned by completionHistory(); `days` = size of Stats::daily.
// Completions dated after `today` (another device's clock running ahead)
// count in `total` only.
Stats computeStats(const QList<Task> &tasks, const QList<Completion> &history, const QDate &today, int days = 14);

} // namespace ztodo
