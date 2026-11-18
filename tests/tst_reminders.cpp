// Reminder scheduling: which tasks to notify about, and only once.
#include "Reminders.hpp"

#include <QTest>

using namespace ztodo;

class TstReminders : public QObject {
    Q_OBJECT

private:
    static Task timed(const QString &id, const QString &due)
    {
        Task t;
        t.id = id;
        t.title = "task " + id;
        parseDueInto(due, &t.dueDate, &t.dueTime);
        return t;
    }
    static QDateTime at(const QString &s) { return QDateTime::fromString(s, "yyyy-MM-ddTHH:mm"); }

private slots:
    void firesOnceAtDueTime()
    {
        ReminderScheduler r;
        const QList<Task> tasks{timed("a", "2026-11-18T09:00")};
        QVERIFY(r.poll(tasks, at("2026-11-18T08:59")).isEmpty());
        QCOMPARE(r.poll(tasks, at("2026-11-18T09:00")).size(), 1);
        QVERIFY(r.poll(tasks, at("2026-11-18T09:01")).isEmpty()); // already fired
    }

    void leadTimeFiresEarly()
    {
        ReminderScheduler r(15);
        const QList<Task> tasks{timed("a", "2026-11-18T09:00")};
        QVERIFY(r.poll(tasks, at("2026-11-18T08:44")).isEmpty());
        QCOMPARE(r.poll(tasks, at("2026-11-18T08:45")).size(), 1);
    }

    void skipsAllDayDoneAndStale()
    {
        ReminderScheduler r(0, 10);
        Task done = timed("d", "2026-11-18T09:00");
        done.done = true;
        const QList<Task> tasks{timed("allday", "2026-11-18"), done, timed("old", "2026-11-18T07:00")};
        QVERIFY(r.poll(tasks, at("2026-11-18T09:05")).isEmpty()); // "old" is hours late: no startup burst
    }

    void reschedulingFiresAgain()
    {
        ReminderScheduler r;
        QCOMPARE(r.poll({timed("a", "2026-11-18T09:00")}, at("2026-11-18T09:00")).size(), 1);
        // A recurring task moved to its next day: a new due moment.
        QCOMPARE(r.poll({timed("a", "2026-11-19T09:00")}, at("2026-11-19T09:00")).size(), 1);
    }

    void orderedByDueTime()
    {
        ReminderScheduler r(0, 30);
        const auto out = r.poll({timed("b", "2026-11-18T09:10"), timed("a", "2026-11-18T09:00")}, at("2026-11-18T09:15"));
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[0].taskId, QString("a"));
    }
};

QTEST_GUILESS_MAIN(TstReminders)
#include "tst_reminders.moc"
