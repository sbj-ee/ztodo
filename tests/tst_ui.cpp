// The main window, driven offscreen: quick add, complete, undo, views,
// projects, and a second "device" writing to the same folder.
#include "Appearance.hpp"
#include "AppFont.hpp"
#include "Delegates.hpp"
#include "MainWindow.hpp"
#include "Store.hpp"
#include "DateText.hpp"
#include "SettingsDialog.hpp"
#include "TaskDialog.hpp"
#include "version.hpp"

#include <QApplication>
#include <QFontInfo>
#include <QComboBox>
#include <QFontComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpression>
#include <QMenuBar>
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
    // The colour the sidebar is painted with (set through its stylesheet).
    static QColor navBackground(QListWidget *nav)
    {
        static const QRegularExpression bg("background:\\s*(#[0-9a-fA-F]{6})");
        const auto m = bg.match(nav->styleSheet());
        return m.hasMatch() ? QColor(m.captured(1)) : QColor();
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

    void appearanceSwitchesThemeAndRestyles()
    {
        QTemporaryDir data;
        Store store;
        QVERIFY(store.open(data.path(), "test-dev"));
        MainWindow w(&store);
        w.show();
        const QColor before = navBackground(w.nav());

        appearance::apply(appearance::Mode::Dark);
        QCoreApplication::processEvents();
        QVERIFY(QApplication::palette().color(QPalette::Window).lightness() < 128);
        // The sidebar follows: it is explicitly coloured, so it must be re-styled.
        QTRY_VERIFY(navBackground(w.nav()).lightness() < 128);
        // The task list too (its background was once set only at startup, leaving
        // light text on a light list).
        QTRY_VERIFY(w.taskList()->palette().color(QPalette::Base).lightness() < 128);

        appearance::apply(appearance::Mode::Light);
        QCoreApplication::processEvents();
        QVERIFY(QApplication::palette().color(QPalette::Window).lightness() >= 128);
        QTRY_VERIFY(navBackground(w.nav()).lightness() >= 128);
        QTRY_VERIFY(w.taskList()->palette().color(QPalette::Base).lightness() >= 128);

        appearance::apply(appearance::Mode::System); // back to what the platform gave us
        QCoreApplication::processEvents();
        QTRY_COMPARE(navBackground(w.nav()).lightness() < 128, before.lightness() < 128);
    }

    void appearanceModeParsing()
    {
        using appearance::Mode;
        QVERIFY(appearance::parse("dark") == Mode::Dark);
        QVERIFY(appearance::parse(" Light ") == Mode::Light);
        QVERIFY(appearance::parse("garbage") == Mode::System);
        QCOMPARE(appearance::toString(Mode::Dark), QString("dark"));
        QVERIFY(appearance::parse(appearance::toString(Mode::System)) == Mode::System);
    }

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

    void sidebarFilledOnOpen()
    {
        // Existing data, no change after the window opens: the sidebar must
        // still list the views, projects and labels with their counts.
        QTemporaryDir data;
        {
            Store before;
            QVERIFY(before.open(data.path(), "test-dev"));
            const QString pid = before.addProject("Work");
            const QString lid = before.addLabel("phone");
            Task t;
            t.title = "Call client";
            t.project = pid;
            t.labels = {lid};
            t.dueDate = QDate::currentDate();
            before.addTask(t);
        }
        Store store;
        QVERIFY(store.open(data.path(), "test-dev"));
        MainWindow w(&store);
        QCOMPARE(navCount(w.nav(), "Inbox"), 0);
        QCOMPARE(navCount(w.nav(), "Today"), 1);
        QCOMPARE(navCount(w.nav(), "Upcoming"), 0);
        QCOMPARE(navCount(w.nav(), "Completed"), 0);
        QCOMPARE(navCount(w.nav(), "Work"), 1);
        QCOMPARE(navCount(w.nav(), "phone"), 1);
        QVERIFY(w.nav()->currentItem()); // the current view is highlighted
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

    void menuBarStaysInWindow()
    {
        // With a global-menu service on the session bus, Qt hides an in-window
        // menu bar unless it is told not to; then Settings is unreachable.
        QTemporaryDir data;
        Store store;
        QVERIFY(store.open(data.path(), "test-dev"));
        MainWindow w(&store);
#if !defined(Q_OS_MACOS)
        QVERIFY(!w.menuBar()->isNativeMenuBar());
#endif
        QStringList menus;
        for (QAction *a : w.menuBar()->actions()) {
            menus << a->text();
        }
        QCOMPARE(menus, (QStringList{"&File", "&Edit", "&View", "&Settings", "&Help"}));
        auto *settings = w.findChild<QMenu *>("settingsMenu");
        QVERIFY(settings);
        auto *prefs = w.findChild<QAction *>("preferencesAction");
        QVERIFY(prefs);
        QCOMPARE(prefs->shortcut(), QKeySequence("Ctrl+,"));
        auto *autoUpdate = w.findChild<QAction *>("autoUpdateAction");
        QVERIFY(autoUpdate && autoUpdate->isCheckable());
        autoUpdate->setChecked(true);
        autoUpdate->setChecked(false);
        QVERIFY(!AppSettings::load().checkForUpdatesOnStartup);
        autoUpdate->setChecked(true);
        QVERIFY(AppSettings::load().checkForUpdatesOnStartup);
    }

    void windowTitleShowsVersion()
    {
        QTemporaryDir data;
        Store store;
        QVERIFY(store.open(data.path(), "test-dev"));
        MainWindow w(&store);
        const QString v = QString::fromLatin1(kVersionString);
        w.setView(View{ViewKind::Inbox, {}, {}});
        QCOMPARE(w.windowTitle(), QString("ztodo %1 — Inbox").arg(v));
        w.setView(View{ViewKind::Today, {}, {}});
        QCOMPARE(w.windowTitle(), QString("ztodo %1 — Today").arg(v));
        QCOMPARE(MainWindow::windowTitleFor(QString()), QString("ztodo %1").arg(v));
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

    void taskDialogMnemonicLabelsHaveBuddies()
    {
        // addRow(QString, QLayout*) used to create Due/Repeat labels with no
        // buddy, so the mnemonic '&' was shown literally in the dialog.
        QTemporaryDir data;
        Store store;
        QVERIFY(store.open(data.path(), "test-dev"));
        Task t;
        t.title = "Sample";
        TaskDialog dlg(&store, t, false);

        auto *dueLabel = dlg.findChild<QLabel *>("dueLabel");
        auto *recurLabel = dlg.findChild<QLabel *>("recurLabel");
        QVERIFY(dueLabel);
        QVERIFY(recurLabel);
        QCOMPARE(dueLabel->buddy(), static_cast<QWidget *>(dlg.dueEdit()));
        auto *recur = dlg.findChild<QLineEdit *>("recur");
        QVERIFY(recur);
        QCOMPARE(recurLabel->buddy(), static_cast<QWidget *>(recur));

        for (auto *label : dlg.findChildren<QLabel *>()) {
            if (label->text().contains(QLatin1Char('&'))) {
                QVERIFY2(label->buddy(),
                         qPrintable(QStringLiteral("label '%1' (%2) has no buddy")
                                        .arg(label->text(), label->objectName())));
            }
        }
    }

    void fontSettingsRoundTrip()
    {
        const AppSettings previous = AppSettings::load();
        AppSettings a = previous;
        QCOMPARE(AppSettings{}.fontFamily, QString());
        QCOMPARE(AppSettings{}.fontSize, QString("medium"));

        a.fontFamily = QStringLiteral("DejaVu Sans");
        a.fontSize = QStringLiteral("large");
        a.save();
        AppSettings b = AppSettings::load();
        QCOMPARE(b.fontFamily, QString("DejaVu Sans"));
        QCOMPARE(b.fontSize, QString("large"));

        a.fontFamily = QStringLiteral("system");
        a.fontSize = QStringLiteral("SMALL");
        a.save();
        b = AppSettings::load();
        QCOMPARE(b.fontFamily, QString());
        QCOMPARE(b.fontSize, QString("small"));

        a.fontSize = QStringLiteral("huge");
        a.save();
        QCOMPARE(AppSettings::load().fontSize, QString("medium"));
        previous.save();
    }

    void fontSizeMapping()
    {
        using appFont::Size;
        QCOMPARE(appFont::pointSizeFor(Size::Medium, 11.0), 11.0);
        QCOMPARE(appFont::pointSizeFor(Size::Small, 11.0), 9.0);
        QCOMPARE(appFont::pointSizeFor(Size::Large, 11.0), 13.0);
        QCOMPARE(appFont::pointSizeFor(Size::Small, 8.0), 8.0);
        QCOMPARE(appFont::pointSizeFor(Size::Small, 9.0), 8.0);
        QVERIFY(appFont::parseSize("small") == Size::Small);
        QVERIFY(appFont::parseSize(" LARGE ") == Size::Large);
        QVERIFY(appFont::parseSize("garbage") == Size::Medium);
        QCOMPARE(appFont::sizeToString(Size::Small), QString("small"));
        QVERIFY(appFont::parseSize(appFont::sizeToString(Size::Medium)) == Size::Medium);
        QVERIFY(appFont::isSystemFamily(QString()));
        QVERIFY(appFont::isSystemFamily(QStringLiteral(" System ")));
        QVERIFY(!appFont::isSystemFamily(QStringLiteral("DejaVu Sans")));
        const QFont resolved = appFont::resolve(QStringLiteral("Times New Roman"), Size::Medium);
        QCOMPARE(resolved.family(), QString("Times New Roman"));
    }

    void fontApplyChangesAppFont()
    {
        appFont::apply(QString(), appFont::Size::Medium);
        const qreal mid = appFont::systemDefault().pointSizeF() > 0
            ? QFontInfo(appFont::systemDefault()).pointSizeF()
            : QFontInfo(QApplication::font()).pointSizeF();
        const qreal large = appFont::pointSizeFor(appFont::Size::Large, mid);
        const qreal small = appFont::pointSizeFor(appFont::Size::Small, mid);

        appFont::apply(QString(), appFont::Size::Large);
        QCOMPARE(QApplication::font().pointSizeF(), large);

        appFont::apply(QString(), appFont::Size::Small);
        QCOMPARE(QApplication::font().pointSizeF(), small);

        appFont::apply(QStringLiteral("Times New Roman"), appFont::Size::Medium);
        QCOMPARE(QApplication::font().family(), QString("Times New Roman"));
        QCOMPARE(QApplication::font().pointSizeF(), appFont::pointSizeFor(appFont::Size::Medium, mid));

        appFont::apply(QString(), appFont::Size::Medium);
        QCOMPARE(QApplication::font().pointSizeF(), appFont::pointSizeFor(appFont::Size::Medium, mid));
    }

    void fontApplyUpdatesMainWindowTitle()
    {
        QTemporaryDir data;
        Store store;
        QVERIFY(store.open(data.path(), "test-dev"));
        MainWindow w(&store);
        w.show();
        appFont::apply(QString(), appFont::Size::Medium);
        QCoreApplication::processEvents();
        auto *title = w.findChild<QLabel *>("viewTitle");
        QVERIFY(title);
        const qreal before = title->font().pointSizeF();
        appFont::apply(QString(), appFont::Size::Large);
        QCoreApplication::processEvents();
        QVERIFY(title->font().pointSizeF() > before);
        appFont::apply(QString(), appFont::Size::Medium);
        QCoreApplication::processEvents();
        QCOMPARE(title->font().pointSizeF(), before);
    }

    void settingsDialogMnemonicLabelsHaveBuddies()
    {
        SettingsDialog dlg(AppSettings{}, false);
        auto *fontLabel = dlg.findChild<QLabel *>("fontFamilyLabel");
        auto *sizeLabel = dlg.findChild<QLabel *>("fontSizeLabel");
        auto *font = dlg.findChild<QFontComboBox *>("fontFamily");
        auto *size = dlg.findChild<QComboBox *>("fontSize");
        QVERIFY(fontLabel);
        QVERIFY(sizeLabel);
        QVERIFY(font);
        QVERIFY(size);
        QCOMPARE(fontLabel->buddy(), static_cast<QWidget *>(font));
        QCOMPARE(sizeLabel->buddy(), static_cast<QWidget *>(size));
        QCOMPARE(font->currentIndex(), 0);
        QCOMPARE(size->currentData().toString(), QString("medium"));

        for (auto *label : dlg.findChildren<QLabel *>()) {
            if (label->text().contains(QLatin1Char('&'))) {
                QVERIFY2(label->buddy(),
                         qPrintable(QStringLiteral("label '%1' (%2) has no buddy")
                                        .arg(label->text(), label->objectName())));
            }
        }
    }
};

QTEST_MAIN(TstUi)
#include "tst_ui.moc"
