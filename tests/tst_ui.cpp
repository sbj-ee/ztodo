// The main window, driven offscreen: quick add, complete, undo, views,
// projects, and a second "device" writing to the same folder.
#include "Delegates.hpp"
#include "MainWindow.hpp"
#include "Store.hpp"
#include "DateText.hpp"
#include "TaskDialog.hpp"

#include <QLineEdit>
#include <QListWidget>
#include <QTemporaryDir>
#include <QTest>

using namespace ztodo;

class TstUi : public QObject {
    Q_OBJECT

    QTemporaryDir m_config;

    static QStringList titles(QListWidget *list)
    {
        QStringList out;
        for (int i = 0; i < list->count(); ++i) {
            if (!list->item(i)->data(RoleIsHeader).toBool()) {
                out << list->item(i)->data(Qt::AccessibleTextRole).toString();
            }
        }
        return out;
    }
    static int navCount(QListWidget *nav, const QString &name)
    {
        for (int i = 0; i < nav->count(); ++i) {
            if (nav->item(i)->text() == name) {
                return nav->item(i)->data(RoleCount).toInt();
            }
        }
        return -1;
    }

private slots:
    void initTestCase() { qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8()); }

    void quickAddCompleteUndo()
    {
        QTemporaryDir data;
        Store store;
        QVERIFY(store.open(data.path(), "test-dev"));
        MainWindow w(&store);
        w.setView(View{ViewKind::Inbox, {}, {}});
        w.show();

        QTest::keyClicks(w.quickAdd(), "Buy milk p1");
        QTest::keyClick(w.quickAdd(), Qt::Key_Return);
        QCOMPARE(titles(w.taskList()), QStringList{"Buy milk"});
        QVERIFY(w.quickAdd()->text().isEmpty());
        QCOMPARE(navCount(w.nav(), "Inbox"), 1);
        const QString id = store.tasks().first().id;
        QCOMPARE(store.tasks().first().priority, 1);

        w.toggleComplete(id);
        QVERIFY(titles(w.taskList()).isEmpty());
        QCOMPARE(navCount(w.nav(), "Inbox"), 0);
        w.undo();
        QCOMPARE(titles(w.taskList()), QStringList{"Buy milk"});
        w.undo(); // the add
        QVERIFY(store.tasks().isEmpty());
    }

    void todayViewDefaultsToToday()
    {
        QTemporaryDir data;
        Store store;
        QVERIFY(store.open(data.path(), "test-dev"));
        MainWindow w(&store);
        w.setView(View{ViewKind::Today, {}, {}});
        w.addFromText("Stretch");
        QCOMPARE(titles(w.taskList()), QStringList{"Stretch"});
        QCOMPARE(store.tasks().first().dueDate, QDate::currentDate());
    }

    void projectsAndLabelsFromQuickAdd()
    {
        QTemporaryDir data;
        Store store;
        QVERIFY(store.open(data.path(), "test-dev"));
        MainWindow w(&store);
        w.addFromText("Fix sink #Home_Improvement @weekend");
        QVERIFY(!store.findProject("Home Improvement").isEmpty());
        QVERIFY(!store.findLabel("weekend").isEmpty());
        QCOMPARE(navCount(w.nav(), "Home Improvement"), 1);
        QCOMPARE(navCount(w.nav(), "weekend"), 1);
        // In a project view, new tasks go to that project.
        const QString pid = store.findProject("Home Improvement");
        w.setView(View{ViewKind::Project, pid, {}});
        w.addFromText("Paint fence");
        QCOMPARE(titles(w.taskList()), (QStringList{"Fix sink", "Paint fence"}));
    }

    void recurringCompleteMovesOn()
    {
        QTemporaryDir data;
        Store store;
        QVERIFY(store.open(data.path(), "test-dev"));
        MainWindow w(&store);
        w.setView(View{ViewKind::Today, {}, {}});
        const QString id = w.addFromText("Vitamins every day");
        Task t;
        QVERIFY(store.task(id, &t));
        QCOMPARE(t.dueDate, QDate::currentDate());
        w.toggleComplete(id);
        QVERIFY(store.task(id, &t));
        QVERIFY(!t.done);
        QCOMPARE(t.dueDate, QDate::currentDate().addDays(1));
        QVERIFY(titles(w.taskList()).isEmpty()); // moved to tomorrow
    }

    void otherDeviceChangesAppear()
    {
        QTemporaryDir data;
        Store store;
        QVERIFY(store.open(data.path(), "dev-a"));
        store.setPollInterval(100);
        MainWindow w(&store);
        w.setView(View{ViewKind::Inbox, {}, {}});
        Store other;
        QVERIFY(other.open(data.path(), "dev-b"));
        Task t;
        t.title = "From the Mac";
        other.addTask(t);
        QTRY_COMPARE_WITH_TIMEOUT(titles(w.taskList()), QStringList{"From the Mac"}, 5000);
    }

    void searchView()
    {
        QTemporaryDir data;
        Store store;
        QVERIFY(store.open(data.path(), "test-dev"));
        MainWindow w(&store);
        w.addFromText("Call the bank");
        w.addFromText("Water plants");
        w.setView(View{ViewKind::Search, {}, "bank"});
        QCOMPARE(titles(w.taskList()), QStringList{"Call the bank"});
    }

    void dueEditTextRoundTrips()
    {
        const QDate today = QDate::currentDate();
        for (int delta : {-40, -2, -1, 0, 1, 3, 6, 7, 30, 400}) {
            const QDate d = today.addDays(delta);
            for (const QTime &tm : {QTime(), QTime(17, 30)}) {
                QDate back;
                QTime backT;
                const QString s = TaskDialog::dueEditText(d, tm, today);
                QVERIFY2(parseDue(s, today, &back, &backT), qPrintable(s));
                QCOMPARE(back, d);
                QCOMPARE(backT, tm);
            }
        }
    }
};

QTEST_MAIN(TstUi)
#include "tst_ui.moc"
