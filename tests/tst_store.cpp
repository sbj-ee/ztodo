// Storage: per-device op logs in a shared folder, merged last-writer-wins.
// Two Store instances on one folder stand in for two devices syncing via Box.
#include "Store.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace ztodo;

class TstStore : public QObject {
    Q_OBJECT

private:
    static Task simple(const QString &title)
    {
        Task t;
        t.title = title;
        return t;
    }
    // One log line, as a device would write it (no raw strings: moc in Qt 6.4
    // mis-parses them).
    static QByteArray line(qint64 ts, const QString &dev, const QString &kind, const QString &id, const QString &title)
    {
        Op op;
        op.ts = ts;
        op.device = dev;
        op.kind = kind;
        op.id = id;
        op.fields.insert("title", title);
        return op.toJsonLine();
    }
    static QByteArray readAll(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private slots:
    void createsLayout()
    {
        QTemporaryDir tmp;
        Store s;
        QVERIFY(s.open(tmp.path() + "/data", "laptop-1"));
        QVERIFY(QFileInfo::exists(tmp.path() + "/data/ztodo.json"));
        QVERIFY(QFileInfo(tmp.path() + "/data/devices").isDir());
        QVERIFY(!s.addTask(simple("Buy milk")).isEmpty());
        QVERIFY(QFileInfo::exists(tmp.path() + "/data/devices/laptop-1.jsonl"));
    }

    void rejectsBadDeviceId()
    {
        QTemporaryDir tmp;
        Store s;
        QString err;
        QVERIFY(!s.open(tmp.path(), "../evil", &err));
        QVERIFY(!err.isEmpty());
    }

    void refusesNewerFormat()
    {
        QTemporaryDir tmp;
        QFile f(tmp.path() + "/ztodo.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"format\":\"ztodo\",\"version\":2}");
        f.close();
        Store s;
        QString err;
        QVERIFY(!s.open(tmp.path(), "a", &err));
        QVERIFY(err.contains("newer"));
    }

    void persistsAcrossReopen()
    {
        QTemporaryDir tmp;
        QString id;
        {
            Store s;
            QVERIFY(s.open(tmp.path(), "dev-a"));
            Task t = simple("Write report");
            t.priority = 1;
            t.dueDate = QDate(2026, 11, 20);
            t.dueTime = QTime(17, 0);
            t.labels = {"l1"};
            t.recur = "every week";
            id = s.addTask(t);
        }
        Store s;
        QVERIFY(s.open(tmp.path(), "dev-a"));
        Task t;
        QVERIFY(s.task(id, &t));
        QCOMPARE(t.title, QString("Write report"));
        QCOMPARE(t.priority, 1);
        QCOMPARE(t.dueDate, QDate(2026, 11, 20));
        QCOMPARE(t.dueTime, QTime(17, 0));
        QCOMPARE(t.labels, QStringList{"l1"});
        QCOMPARE(t.recur, QString("every week"));
    }

    void twoDevicesMerge()
    {
        QTemporaryDir tmp;
        Store a;
        Store b;
        QVERIFY(a.open(tmp.path(), "dev-a"));
        QVERIFY(b.open(tmp.path(), "dev-b"));
        const QString id = a.addTask(simple("Shared"));
        QVERIFY(b.reload());
        Task t;
        QVERIFY(b.task(id, &t));
        QCOMPARE(t.title, QString("Shared"));
        // Each device writes only its own file.
        QCOMPARE(Store::logFileCount(tmp.path()), 1);
        QVERIFY(b.setTaskFields(id, {{"notes", "from b"}}));
        QCOMPARE(Store::logFileCount(tmp.path()), 2);
        QVERIFY(a.reload());
        QVERIFY(a.task(id, &t));
        QCOMPARE(t.notes, QString("from b"));
    }

    void lastWriterWinsPerField()
    {
        QTemporaryDir tmp;
        Store a;
        Store b;
        qint64 clockA = 1000;
        qint64 clockB = 1000;
        a.setClockForTests([&] { return clockA; });
        b.setClockForTests([&] { return clockB; });
        QVERIFY(a.open(tmp.path(), "dev-a"));
        QVERIFY(b.open(tmp.path(), "dev-b"));
        const QString id = a.addTask(simple("Original"));
        b.reload();
        // Offline edits on both devices: different fields both survive; the
        // same field goes to the later write.
        clockA = 5000;
        clockB = 6000;
        QVERIFY(a.setTaskFields(id, {{"title", "A title"}, {"priority", 2}}));
        QVERIFY(b.setTaskFields(id, {{"title", "B title"}, {"notes", "B notes"}}));
        a.reload();
        b.reload();
        Task ta;
        Task tb;
        QVERIFY(a.task(id, &ta));
        QVERIFY(b.task(id, &tb));
        QCOMPARE(ta.title, QString("B title"));
        QCOMPARE(ta.priority, 2);
        QCOMPARE(ta.notes, QString("B notes"));
        QCOMPARE(tb.title, ta.title);
        QCOMPARE(tb.priority, ta.priority);
        QCOMPARE(tb.notes, ta.notes);
    }

    void clockNeverGoesBackwards()
    {
        // A device whose wall clock is behind still overrides what it has seen.
        QTemporaryDir tmp;
        Store a;
        Store b;
        a.setClockForTests([] { return qint64(900000); });
        b.setClockForTests([] { return qint64(100); }); // far behind
        QVERIFY(a.open(tmp.path(), "dev-a"));
        QVERIFY(b.open(tmp.path(), "dev-b"));
        const QString id = a.addTask(simple("x"));
        b.reload();
        QVERIFY(b.setTaskFields(id, {{"title", "edited on b"}}));
        a.reload();
        Task t;
        QVERIFY(a.task(id, &t));
        QCOMPARE(t.title, QString("edited on b"));
    }

    void tieBreaksOnDeviceId()
    {
        QTemporaryDir tmp;
        Store a;
        Store b;
        a.setClockForTests([] { return qint64(5000); });
        b.setClockForTests([] { return qint64(5000); });
        QVERIFY(a.open(tmp.path(), "dev-a"));
        QVERIFY(b.open(tmp.path(), "dev-b"));
        QVERIFY(a.put("task", "t1", {{"title", "from a"}}));
        QVERIFY(b.put("task", "t1", {{"title", "from b"}}));
        a.reload();
        b.reload();
        Task ta;
        Task tb;
        QVERIFY(a.task("t1", &ta));
        QVERIFY(b.task("t1", &tb));
        QCOMPARE(ta.title, tb.title); // same answer on both devices
        QCOMPARE(ta.title, QString("from b"));
    }

    void ignoresTornAndGarbageLines()
    {
        QTemporaryDir tmp;
        QDir().mkpath(tmp.path() + "/devices");
        QFile f(tmp.path() + "/devices/other-1.jsonl");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not json\n");
        f.write(line(10, "other-1", "task", "t1", "ok"));
        f.write(line(11, "other-1", "task", "t2", "bad kind").replace("\"task\"", "\"bogus\""));
        const QByteArray full = line(12, "other-1", "task", "t1", "torn fixed");
        const qsizetype cut = full.indexOf(" fixed");
        f.write(full.left(cut)); // no newline yet
        f.close();
        Store s;
        QVERIFY(s.open(tmp.path(), "me"));
        QCOMPARE(s.tasks().size(), 1);
        QCOMPARE(s.tasks().first().title, QString("ok"));
        // The rest of the line arrives (sync finished).
        QVERIFY(f.open(QIODevice::Append));
        f.write(full.mid(cut));
        f.close();
        QTest::qWait(20); // mtime granularity
        QVERIFY(f.open(QIODevice::Append));
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(5), QFileDevice::FileModificationTime));
        f.close();
        QVERIFY(s.reload());
        QCOMPARE(s.tasks().first().title, QString("torn fixed"));
    }

    void ownTornLineIsTerminated()
    {
        QTemporaryDir tmp;
        Store s;
        QVERIFY(s.open(tmp.path(), "me"));
        QFile f(s.ownLogPath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(line(1, "me", "task", "x", "lost").left(30)); // crash mid-write
        f.close();
        const QString id = s.addTask(simple("after crash"));
        Store other;
        QVERIFY(other.open(tmp.path(), "other"));
        Task t;
        QVERIFY(other.task(id, &t));
    }

    void readsConflictCopies()
    {
        // rclone bisync renames a conflicting file to *.jsonl.conflict1; the
        // ops in it must still count.
        QTemporaryDir tmp;
        QDir().mkpath(tmp.path() + "/devices");
        QFile f(tmp.path() + "/devices/x-1.jsonl.conflict1");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(line(10, "x-1", "task", "t9", "from conflict copy"));
        f.close();
        Store s;
        QVERIFY(s.open(tmp.path(), "me"));
        QCOMPARE(s.tasks().size(), 1);
    }

    void deleteHidesTaskAndSubtasks()
    {
        QTemporaryDir tmp;
        Store s;
        QVERIFY(s.open(tmp.path(), "me"));
        const QString parent = s.addTask(simple("parent"));
        Task child = simple("child");
        child.parent = parent;
        s.addTask(child);
        QCOMPARE(s.tasks().size(), 2);
        QVERIFY(s.remove("task", parent));
        QCOMPARE(s.tasks().size(), 0);
    }

    void completeAndUndo()
    {
        QTemporaryDir tmp;
        Store s;
        QVERIFY(s.open(tmp.path(), "me"));
        const QString id = s.addTask(simple("once"));
        const QJsonObject before = s.complete(id);
        Task t;
        QVERIFY(s.task(id, &t));
        QVERIFY(t.done);
        QVERIFY(t.doneAt.isValid());
        QVERIFY(s.setTaskFields(id, before)); // undo
        QVERIFY(s.task(id, &t));
        QVERIFY(!t.done);
    }

    void completeRecurringMovesDate()
    {
        QTemporaryDir tmp;
        Store s;
        QVERIFY(s.open(tmp.path(), "me"));
        Task t = simple("Water plants");
        t.dueDate = QDate(2026, 11, 12);
        t.recur = "every 3 days";
        const QString id = s.addTask(t);
        s.complete(id, QDate(2026, 11, 12));
        QVERIFY(s.task(id, &t));
        QVERIFY(!t.done);
        QCOMPARE(t.dueDate, QDate(2026, 11, 15));
    }

    void findByName()
    {
        QTemporaryDir tmp;
        Store s;
        QVERIFY(s.open(tmp.path(), "me"));
        const QString p = s.addProject("Home Improvement");
        QCOMPARE(s.findProject("home_improvement"), p);
        QCOMPARE(s.findProject("HomeImprovement"), p);
        QVERIFY(s.findProject("garden").isEmpty());
        const QString l = s.addLabel("errand");
        QCOMPARE(s.findLabel("Errand"), l);
        QCOMPARE(s.projects().size(), 1);
        s.remove("project", p);
        QCOMPARE(s.projects().size(), 0);
    }

    void compactionKeepsState()
    {
        QTemporaryDir tmp;
        Store a;
        Store b;
        QVERIFY(a.open(tmp.path(), "dev-a"));
        QVERIFY(b.open(tmp.path(), "dev-b"));
        const QString id = a.addTask(simple("v0"));
        for (int i = 1; i <= 50; ++i) {
            a.setTaskFields(id, {{"title", QString("v%1").arg(i)}});
        }
        b.reload();
        b.setTaskFields(id, {{"notes", "b"}});
        a.reload();
        const qint64 before = QFileInfo(a.ownLogPath()).size();
        QVERIFY(a.compactOwnLog() > 0);
        QVERIFY(QFileInfo(a.ownLogPath()).size() < before);
        // A fresh reader sees the same thing.
        Store c;
        QVERIFY(c.open(tmp.path(), "dev-c"));
        Task t;
        QVERIFY(c.task(id, &t));
        QCOMPARE(t.title, QString("v50"));
        QCOMPARE(t.notes, QString("b"));
        QCOMPARE(t.priority, 4);
    }

    void changedSignalOnExternalWrite()
    {
        QTemporaryDir tmp;
        Store a;
        Store b;
        QVERIFY(a.open(tmp.path(), "dev-a"));
        QVERIFY(b.open(tmp.path(), "dev-b"));
        b.setPollInterval(100);
        QSignalSpy spy(&b, &Store::changed);
        a.addTask(simple("ping"));
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 3000);
        QCOMPARE(b.tasks().size(), 1);
    }
};

QTEST_GUILESS_MAIN(TstStore)
#include "tst_store.moc"
