// Natural-language dates, recurrence rules and quick add.
#include "DateText.hpp"
#include "QuickAdd.hpp"
#include "Recurrence.hpp"

#include <QTest>

using namespace ztodo;

// Thursday 12 November 2026.
static const QDate kToday(2026, 11, 12);

class TstParsing : public QObject {
    Q_OBJECT

private slots:
    void dates_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QDate>("date");
        QTest::newRow("today") << "today" << kToday;
        QTest::newRow("tod") << "tod" << kToday;
        QTest::newRow("tomorrow") << "Tomorrow" << kToday.addDays(1);
        QTest::newRow("tmr") << "tmr" << kToday.addDays(1);
        QTest::newRow("same weekday") << "thursday" << kToday;
        QTest::newRow("next same weekday") << "next thu" << kToday.addDays(7);
        QTest::newRow("fri") << "fri" << QDate(2026, 11, 13);
        QTest::newRow("monday") << "monday" << QDate(2026, 11, 16);
        QTest::newRow("next week") << "next week" << QDate(2026, 11, 16);
        QTest::newRow("next month") << "next month" << QDate(2026, 12, 1);
        QTest::newRow("in 3 days") << "in 3 days" << QDate(2026, 11, 15);
        QTest::newRow("in 2 weeks") << "in 2 weeks" << QDate(2026, 11, 26);
        QTest::newRow("in 1 month") << "in 1 month" << QDate(2026, 12, 12);
        QTest::newRow("eom") << "eom" << QDate(2026, 11, 30);
        QTest::newRow("nov 20") << "nov 20" << QDate(2026, 11, 20);
        QTest::newRow("November 20th") << "November 20th" << QDate(2026, 11, 20);
        QTest::newRow("20 nov") << "20 nov" << QDate(2026, 11, 20);
        QTest::newRow("past month-day -> next year") << "jan 5" << QDate(2027, 1, 5);
        QTest::newRow("explicit year") << "jan 5 2028" << QDate(2028, 1, 5);
        QTest::newRow("us slash") << "12/25" << QDate(2026, 12, 25);
        QTest::newRow("us slash year") << "3/4/27" << QDate(2027, 3, 4);
        QTest::newRow("iso") << "2027-02-03" << QDate(2027, 2, 3);
    }
    void dates()
    {
        QFETCH(QString, text);
        QFETCH(QDate, date);
        const TextMatch m = findDate(text, kToday);
        QVERIFY2(m.found(), qPrintable(text));
        QCOMPARE(m.date, date);
        QCOMPARE(m.start, 0);
        QCOMPARE(m.length, text.size());
    }

    void noFalseDates()
    {
        QVERIFY(!findDate("Review monthly budget", kToday).found());
        QVERIFY(!findDate("Fix the sunroof", kToday).found());
        QVERIFY(!findDate("Mondays are hard", kToday).found());
        QVERIFY(!findDate("Version 13/45", kToday).found()); // not a valid month
    }

    void times_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QTime>("time");
        QTest::newRow("5pm") << "5pm" << QTime(17, 0);
        QTest::newRow("5:30 pm") << "5:30 pm" << QTime(17, 30);
        QTest::newRow("12am") << "12am" << QTime(0, 0);
        QTest::newRow("12pm") << "12pm" << QTime(12, 0);
        QTest::newRow("at 17:15") << "at 17:15" << QTime(17, 15);
        QTest::newRow("9:05") << "9:05" << QTime(9, 5);
        QTest::newRow("at 9") << "at 9" << QTime(9, 0);
        QTest::newRow("at 3") << "at 3" << QTime(15, 0);
        QTest::newRow("noon") << "noon" << QTime(12, 0);
    }
    void times()
    {
        QFETCH(QString, text);
        QFETCH(QTime, time);
        const TextMatch m = findTime(text);
        QVERIFY2(m.found(), qPrintable(text));
        QCOMPARE(m.time, time);
    }

    void parseDueWhole()
    {
        QDate d;
        QTime t;
        QVERIFY(parseDue("tomorrow 5pm", kToday, &d, &t));
        QCOMPARE(d, kToday.addDays(1));
        QCOMPARE(t, QTime(17, 0));
        QVERIFY(parseDue("", kToday, &d, &t));
        QVERIFY(!d.isValid());
        QVERIFY(!parseDue("tomorrow banana", kToday, &d, &t));
        QVERIFY(parseDue("3pm", kToday, &d, &t));
        QCOMPARE(d, kToday);
    }

    void format()
    {
        QCOMPARE(formatDue(kToday, {}, kToday), QString("Today"));
        QCOMPARE(formatDue(kToday.addDays(1), QTime(17, 0), kToday), QString("Tomorrow 5:00 PM"));
        QCOMPARE(formatDue(kToday.addDays(-1), {}, kToday), QString("Yesterday"));
        QCOMPARE(formatDue(QDate(2026, 11, 16), {}, kToday), QString("Monday"));
        QCOMPARE(formatDue(QDate(2026, 12, 25), {}, kToday), QString("Dec 25"));
        QCOMPARE(formatDue(QDate(2027, 1, 5), {}, kToday), QString("Jan 5 2027"));
    }

    void recurrence_data()
    {
        QTest::addColumn<QString>("rule");
        QTest::addColumn<QDate>("base");
        QTest::addColumn<QDate>("next");
        QTest::newRow("every day") << "every day" << kToday << kToday.addDays(1);
        QTest::newRow("daily") << "daily" << kToday << kToday.addDays(1);
        QTest::newRow("every other day") << "every other day" << kToday << kToday.addDays(2);
        QTest::newRow("every 3 days") << "Every 3 Days" << kToday << kToday.addDays(3);
        QTest::newRow("weekly") << "weekly" << kToday << kToday.addDays(7);
        QTest::newRow("every 2 weeks") << "every 2 weeks" << kToday << kToday.addDays(14);
        QTest::newRow("monthly") << "every month" << QDate(2026, 1, 31) << QDate(2026, 2, 28);
        QTest::newRow("every 15th before") << "every 15th" << kToday << QDate(2026, 11, 15);
        QTest::newRow("every 1st") << "every 1st" << kToday << QDate(2026, 12, 1);
        QTest::newRow("every 31st short month") << "every 31st" << QDate(2026, 11, 1) << QDate(2026, 11, 30);
        QTest::newRow("yearly") << "yearly" << kToday << QDate(2027, 11, 12);
        QTest::newRow("weekday from fri") << "every weekday" << QDate(2026, 11, 13) << QDate(2026, 11, 16);
        QTest::newRow("weekend") << "every weekend" << kToday << QDate(2026, 11, 14);
        QTest::newRow("mon wed fri") << "every mon, wed and fri" << kToday << QDate(2026, 11, 13);
        QTest::newRow("tuesday") << "every tuesday" << kToday << QDate(2026, 11, 17);
    }
    void recurrence()
    {
        QFETCH(QString, rule);
        QFETCH(QDate, base);
        QFETCH(QDate, next);
        const Recurrence r = Recurrence::parse(rule);
        QVERIFY2(r.valid, qPrintable(rule));
        QCOMPARE(r.next(base), next);
    }

    void recurrenceInvalid()
    {
        QVERIFY(!Recurrence::parse("").valid);
        QVERIFY(!Recurrence::parse("sometimes").valid);
        QVERIFY(!Recurrence::parse("every blue moon").valid);
        QVERIFY(!Recurrence::parse("every 0 days").valid);
        QVERIFY(!Recurrence::parse("every 40th").valid);
    }

    void afterCompletion()
    {
        // Overdue daily task done today -> tomorrow, not the day after it was due.
        const Recurrence daily = Recurrence::parse("every day");
        QCOMPARE(daily.afterCompletion(kToday.addDays(-5), kToday), kToday.addDays(1));
        // Weekly done early -> a week after the due date.
        const Recurrence weekly = Recurrence::parse("every week");
        QCOMPARE(weekly.afterCompletion(kToday.addDays(2), kToday), kToday.addDays(9));
        // every! counts from today.
        const Recurrence bang = Recurrence::parse("every! 3 days");
        QVERIFY(bang.fromCompletion);
        QCOMPARE(bang.afterCompletion(kToday.addDays(-10), kToday), kToday.addDays(3));
    }

    void firstOccurrence()
    {
        QCOMPARE(Recurrence::parse("every day").first(kToday), kToday);
        QCOMPARE(Recurrence::parse("every monday").first(kToday), QDate(2026, 11, 16));
        QCOMPARE(Recurrence::parse("every thu").first(kToday), kToday);
        QCOMPARE(Recurrence::parse("every 12th").first(kToday), kToday);
        QCOMPARE(Recurrence::parse("every 1st").first(kToday), QDate(2026, 12, 1));
    }

    void quickAdd()
    {
        QuickAdd q = QuickAdd::parse("Call mom tomorrow at 5pm p1 #Family @phone", kToday);
        QCOMPARE(q.title, QString("Call mom"));
        QCOMPARE(q.due, kToday.addDays(1));
        QCOMPARE(q.time, QTime(17, 0));
        QCOMPARE(q.priority, 1);
        QCOMPARE(q.project, QString("Family"));
        QCOMPARE(q.labels, QStringList{"phone"});
        QVERIFY(q.recur.isEmpty());
    }

    void quickAddRecurring()
    {
        QuickAdd q = QuickAdd::parse("Standup every weekday 9:30am #Work", kToday);
        QCOMPARE(q.title, QString("Standup"));
        QCOMPARE(q.recur, QString("every weekday"));
        QCOMPARE(q.due, kToday);
        QCOMPARE(q.time, QTime(9, 30));
        QCOMPARE(q.project, QString("Work"));

        q = QuickAdd::parse("Pay rent every 1st p2 @bills @home_office", kToday);
        QCOMPARE(q.title, QString("Pay rent"));
        QCOMPARE(q.recur, QString("every 1st"));
        QCOMPARE(q.due, QDate(2026, 12, 1));
        QCOMPARE(q.priority, 2);
        QCOMPARE(q.labels, (QStringList{"bills", "home office"}));

        q = QuickAdd::parse("Gym every mon, wed and fri", kToday);
        QCOMPARE(q.title, QString("Gym"));
        QCOMPARE(q.recur, QString("every mon, wed and fri"));
        QCOMPARE(q.due, QDate(2026, 11, 13));
    }

    void quickAddPlain()
    {
        QuickAdd q = QuickAdd::parse("Review monthly budget", kToday);
        QCOMPARE(q.title, QString("Review monthly budget"));
        QVERIFY(!q.due.isValid());
        QCOMPARE(q.priority, 4);
        q = QuickAdd::parse("Email p1@example.com about #3", kToday);
        QCOMPARE(q.priority, 4); // "p1@..." is not a priority token
        q = QuickAdd::parse("today", kToday); // nothing left: keep the text
        QCOMPARE(q.title, QString("today"));
        q = QuickAdd::parse("Dentist on friday", kToday);
        QCOMPARE(q.title, QString("Dentist"));
        QCOMPARE(q.due, QDate(2026, 11, 13));
    }
};

QTEST_GUILESS_MAIN(TstParsing)
#include "tst_parsing.moc"
