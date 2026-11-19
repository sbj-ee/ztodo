// Productivity stats: counting periods, streaks, projects, and history from
// before completion records existed.
#include "Stats.hpp"

#include <QTest>

using namespace ztodo;

static const QDate kToday(2026, 11, 19); // a Thursday

namespace {
// Local noon on `daysAgo` days before kToday.
QDateTime at(int daysAgo, int hour = 12)
{
    return QDateTime(kToday.addDays(-daysAgo), QTime(hour, 0)).toUTC();
}

Completion done(const QString &task, int daysAgo, const QString &project = {})
{
    static int n = 0;
    return Completion{QStringLiteral("c%1").arg(++n), task, project, at(daysAgo)};
}

Task open(const QString &id, const QDate &due = {})
{
    Task t;
    t.id = id;
    t.title = id;
    t.dueDate = due;
    return t;
}
} // namespace

class TstStats : public QObject {
    Q_OBJECT

private slots:
    void empty()
    {
        const Stats s = computeStats({}, {}, kToday);
        QCOMPARE(s.today, 0);
        QCOMPARE(s.total, 0);
        QCOMPARE(s.currentStreak, 0);
        QCOMPARE(s.longestStreak, 0);
        QVERIFY(!s.bestDay.isValid());
        QCOMPARE(s.daily, QList<int>(14, 0));
        QVERIFY(s.byProject30.isEmpty());
    }

    void periods()
    {
        // Thu (today) x2, Mon x1 (this week), last Sat (7-day window, not this
        // week), 20 days ago, 40 days ago.
        const QList<Completion> h{done("a", 0), done("b", 0), done("c", 3), done("d", 5),
                                  done("e", 20), done("f", 40)};
        const Stats s = computeStats({}, h, kToday);
        QCOMPARE(s.today, 2);
        QCOMPARE(s.thisWeek, 3);
        QCOMPARE(s.last7, 4);
        QCOMPARE(s.last30, 5);
        QCOMPARE(s.total, 6);
        QCOMPARE(s.avgPerDay30, 5 / 30.0);
        QCOMPARE(s.daily.size(), 14);
        QCOMPARE(s.daily.last(), 2);
        QCOMPARE(s.daily.at(13 - 3), 1);
        QCOMPARE(s.daily.at(13 - 5), 1);
        QCOMPARE(s.bestDay, kToday);
        QCOMPARE(s.bestDayCount, 2);
    }

    void dayBoundariesAreLocal()
    {
        // 00:30 and 23:30 local on the same day: one day, not two.
        const QList<Completion> h{Completion{"1", "a", {}, at(1, 0).addSecs(1800)},
                                  Completion{"2", "b", {}, at(1, 23).addSecs(1800)}};
        const Stats s = computeStats({}, h, kToday);
        QCOMPARE(s.daily.at(12), 2);
        QCOMPARE(s.longestStreak, 1);
    }

    void futureCountsOnlyInTotal()
    {
        const Stats s = computeStats({}, {done("a", -2)}, kToday);
        QCOMPARE(s.total, 1);
        QCOMPARE(s.today, 0);
        QCOMPARE(s.last30, 0);
        QCOMPARE(s.currentStreak, 0);
    }

    void streaks()
    {
        // Yesterday, 2 and 3 days ago: the streak is alive until today ends.
        QList<Completion> h{done("a", 1), done("b", 2), done("c", 3), // run of 3
                            done("d", 10), done("e", 11), done("f", 12), done("g", 13), done("h", 14)}; // run of 5
        Stats s = computeStats({}, h, kToday);
        QCOMPARE(s.currentStreak, 3);
        QCOMPARE(s.longestStreak, 5);
        h << done("i", 0);
        s = computeStats({}, h, kToday);
        QCOMPARE(s.currentStreak, 4);
        // A missed day breaks it.
        s = computeStats({}, {done("a", 2), done("b", 3)}, kToday);
        QCOMPARE(s.currentStreak, 0);
        QCOMPARE(s.longestStreak, 2);
    }

    void byProject()
    {
        const QList<Completion> h{done("a", 0, "work"), done("b", 1, "work"), done("c", 2),
                                  done("d", 3, "home"), done("e", 40, "home")};
        const Stats s = computeStats({}, h, kToday);
        QCOMPARE(s.byProject30.size(), 3);
        QCOMPARE(s.byProject30.at(0).project, QStringLiteral("work"));
        QCOMPARE(s.byProject30.at(0).count, 2);
        // Ties by id: "" (Inbox) before "home".
        QCOMPARE(s.byProject30.at(1).project, QString());
        QCOMPARE(s.byProject30.at(2).project, QStringLiteral("home"));
        QCOMPARE(s.byProject30.at(2).count, 1);
    }

    void openTasks()
    {
        Task d = open("done", kToday.addDays(-3));
        d.done = true;
        const QList<Task> tasks{open("a"), open("late", kToday.addDays(-1)), open("now", kToday),
                                open("later", kToday.addDays(2)), d};
        const Stats s = computeStats(tasks, {}, kToday);
        QCOMPARE(s.open, 4);
        QCOMPARE(s.overdue, 1);
        QCOMPARE(s.dueToday, 1);
    }

    void historyBackfillsFromDoneAt()
    {
        // Completed before records existed: counted from doneAt.
        Task old = open("old");
        old.done = true;
        old.doneAt = at(2);
        old.project = "p";
        // Recurring, last completed before records existed: its latest doneAt.
        Task rec = open("rec", kToday.addDays(3));
        rec.recur = "every 3 days";
        rec.doneAt = at(1, 10);
        // Recurring with a record for its latest completion: not twice.
        Task rec2 = open("rec2", kToday.addDays(1));
        rec2.recur = "every day";
        rec2.doneAt = at(0);
        // Reopened: doneAt left from a client that didn't clear it, done=false.
        Task reopened = open("reopened");
        reopened.doneAt = at(4);
        const QList<Completion> records{Completion{"r1", "rec2", {}, at(0)}, Completion{"r0", "rec2", {}, at(1)}};

        const QList<Completion> h = completionHistory({old, rec, rec2, reopened}, records);
        QCOMPARE(h.size(), 4);
        QStringList tasks;
        for (const Completion &c : h) {
            tasks << c.task;
        }
        QCOMPARE(tasks, (QStringList{"old", "rec", "rec2", "rec2"})); // oldest first
        QCOMPARE(h.first().project, QStringLiteral("p"));
    }
};

QTEST_GUILESS_MAIN(TstStats)
#include "tst_stats.moc"
