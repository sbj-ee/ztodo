// Which tasks each view shows, in what order and under which headers.
#include "Views.hpp"

#include <QTest>

using namespace ztodo;

static const QDate kToday(2026, 11, 12);

namespace {
Task mk(const QString &id, const QString &title, double order)
{
    Task t;
    t.id = id;
    t.title = title;
    t.order = order;
    return t;
}

QStringList shape(const QList<Row> &rows)
{
    QStringList out;
    for (const Row &r : rows) {
        out << (r.header ? QStringLiteral("[") + r.headerText + QStringLiteral("]")
                         : QString(r.depth * 2, QLatin1Char(' ')) + r.task.title);
    }
    return out;
}
} // namespace

class TstViews : public QObject {
    Q_OBJECT

private slots:
    void inboxNestsSubtasks()
    {
        Task a = mk("a", "A", 1);
        Task b = mk("b", "B", 2);
        Task c = mk("c", "C child of A", 3);
        c.parent = "a";
        Task d = mk("d", "D grandchild", 4);
        d.parent = "c";
        Task p = mk("p", "In project", 5);
        p.project = "proj";
        Task done = mk("x", "done", 6);
        done.done = true;
        const QList<Task> all{a, b, c, d, p, done};
        QCOMPARE(shape(buildRows(all, {ViewKind::Inbox, {}, {}}, kToday)),
                 (QStringList{"A", "  C child of A", "    D grandchild", "B"}));
        QCOMPARE(shape(buildRows(all, {ViewKind::Project, "proj", {}}, kToday)), QStringList{"In project"});
        QCOMPARE(countFor(all, {ViewKind::Inbox, {}, {}}, kToday), 4);
    }

    void parentCycleDoesNotHang()
    {
        Task a = mk("a", "A", 1);
        a.parent = "b";
        Task b = mk("b", "B", 2);
        b.parent = "a";
        const QList<Row> rows = buildRows({a, b}, {ViewKind::Inbox, {}, {}}, kToday);
        QVERIFY(rows.size() <= 2);
    }

    void todayOverdueFirst()
    {
        Task late = mk("l", "late", 1);
        late.dueDate = kToday.addDays(-2);
        Task p1 = mk("p1", "urgent", 2);
        p1.dueDate = kToday;
        p1.priority = 1;
        Task p4 = mk("p4", "whenever", 3);
        p4.dueDate = kToday;
        Task timed = mk("t", "at nine", 4);
        timed.dueDate = kToday;
        timed.dueTime = QTime(9, 0);
        Task later = mk("f", "future", 5);
        later.dueDate = kToday.addDays(1);
        const QList<Task> all{late, p4, timed, p1, later};
        QCOMPARE(shape(buildRows(all, {ViewKind::Today, {}, {}}, kToday)),
                 (QStringList{"[Overdue]", "late", "[Thu Nov 12 · Today]", "urgent", "at nine", "whenever"}));
        QCOMPARE(countFor(all, {ViewKind::Today, {}, {}}, kToday), 4);
    }

    void upcomingGroupsByDay()
    {
        Task a = mk("a", "tomorrow", 1);
        a.dueDate = kToday.addDays(1);
        Task b = mk("b", "next year", 2);
        b.dueDate = QDate(2027, 1, 4);
        Task c = mk("c", "also tomorrow", 3);
        c.dueDate = kToday.addDays(1);
        Task today = mk("d", "today", 4);
        today.dueDate = kToday;
        QCOMPARE(shape(buildRows({b, a, c, today}, {ViewKind::Upcoming, {}, {}}, kToday)),
                 (QStringList{"[Fri Nov 13 · Tomorrow]", "tomorrow", "also tomorrow", "[Mon Jan 4 2027]", "next year"}));
    }

    void labelAndSearch()
    {
        Task a = mk("a", "Buy milk", 1);
        a.labels = {"errand"};
        Task b = mk("b", "Call bank", 2);
        b.notes = "about the MILK account";
        Task c = mk("c", "Old milk", 3);
        c.done = true;
        QCOMPARE(shape(buildRows({a, b, c}, {ViewKind::Label, "errand", {}}, kToday)), QStringList{"Buy milk"});
        QCOMPARE(shape(buildRows({c, a, b}, {ViewKind::Search, {}, "milk"}, kToday)),
                 (QStringList{"Buy milk", "Call bank", "Old milk"}));
        QVERIFY(buildRows({a}, {ViewKind::Search, {}, "  "}, kToday).isEmpty());
    }

    void completedNewestFirst()
    {
        Task a = mk("a", "first", 1);
        a.done = true;
        a.doneAt = QDateTime(kToday, QTime(9, 0));
        Task b = mk("b", "second", 2);
        b.done = true;
        b.doneAt = QDateTime(kToday, QTime(10, 0));
        Task c = mk("c", "yesterday", 3);
        c.done = true;
        c.doneAt = QDateTime(kToday.addDays(-1), QTime(10, 0));
        QCOMPARE(shape(buildRows({a, b, c}, {ViewKind::Completed, {}, {}}, kToday)),
                 (QStringList{"[Thu Nov 12 · Today]", "second", "first", "[Wed Nov 11 · Yesterday]", "yesterday"}));
    }
};

QTEST_GUILESS_MAIN(TstViews)
#include "tst_views.moc"
