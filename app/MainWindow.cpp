#include "MainWindow.hpp"

#include "DateText.hpp"
#include "Delegates.hpp"
#include "QuickAdd.hpp"
#include "Recurrence.hpp"
#include "SettingsDialog.hpp"
#include "TaskDialog.hpp"
#include "Theme.hpp"
#include "UpdateManager.hpp"
#include "version.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

namespace ztodo {

namespace {
const QString kTask = QStringLiteral("task");
const QString kProject = QStringLiteral("project");
const QString kLabel = QStringLiteral("label");

QString quoted(const QString &s)
{
    return QStringLiteral("“%1”").arg(s.size() > 60 ? s.left(57) + QStringLiteral("…") : s);
}
} // namespace

QString MainWindow::windowTitleFor(const QString &viewTitle)
{
    // "ztodo 0.1.2 — Today": which version is running, at a glance.
    const QString app = QStringLiteral("ztodo %1").arg(QString::fromLatin1(kVersionString));
    return viewTitle.isEmpty() ? app : app + QStringLiteral(" — ") + viewTitle;
}

MainWindow::MainWindow(Store *store, QWidget *parent)
    : QMainWindow(parent)
    , m_store(store)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(windowTitleFor(QString()));

    // --- sidebar ---
    m_nav = new QListWidget;
    m_nav->setObjectName(QStringLiteral("nav"));
    m_nav->setItemDelegate(new NavDelegate(m_nav));
    m_nav->setMouseTracking(true);
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setContextMenuPolicy(Qt::CustomContextMenu);
    m_nav->setMinimumWidth(190);
    m_nav->setAutoFillBackground(true);
    {
        QPalette p = m_nav->palette();
        const QColor base = p.color(QPalette::Window);
        p.setColor(QPalette::Base, theme::isDark() ? base.darker(115) : base.lighter(102));
        m_nav->setPalette(p);
    }
    connect(m_nav, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *it) {
        if (!it || m_refreshing) {
            return;
        }
        const int kind = it->data(RoleNavKind).toInt();
        if (kind == -2) {
            addProject();
            return;
        }
        if (kind < 0) {
            return;
        }
        m_search->clear();
        setView(View{static_cast<ViewKind>(kind), it->data(RoleNavId).toString(), {}});
    });
    connect(m_nav, &QListWidget::customContextMenuRequested, this, &MainWindow::navContextMenu);

    // --- main pane ---
    auto *pane = new QWidget;
    auto *v = new QVBoxLayout(pane);
    v->setContentsMargins(28, 18, 28, 6);
    v->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("viewTitle"));
    QFont hf = m_title->font();
    hf.setPointSizeF(hf.pointSizeF() * 1.6);
    hf.setBold(true);
    m_title->setFont(hf);
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("search"));
    m_search->setPlaceholderText(QStringLiteral("Search (Ctrl+F)"));
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(260);
    headerRow->addWidget(m_title, 1);
    headerRow->addWidget(m_search);
    v->addLayout(headerRow);

    m_quick = new QLineEdit;
    m_quick->setObjectName(QStringLiteral("quickAdd"));
    m_quick->setPlaceholderText(
        QStringLiteral("+ Add task   e.g. “Call mom tomorrow 5pm p1 #Family @phone” or “Gym every mon and thu”"));
    m_quick->setMinimumHeight(34);
    v->addWidget(m_quick);
    m_quickPreview = new QLabel;
    m_quickPreview->setObjectName(QStringLiteral("quickPreview"));
    m_quickPreview->setTextFormat(Qt::RichText);
    m_quickPreview->hide();
    v->addWidget(m_quickPreview);

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("tasks"));
    m_delegate = new TaskDelegate(m_store, m_list);
    m_list->setItemDelegate(m_delegate);
    m_list->setMouseTracking(true);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->installEventFilter(this);
    m_list->setAutoFillBackground(false);
    {
        QPalette p = m_list->palette();
        p.setColor(QPalette::Base, p.color(QPalette::Window));
        m_list->setPalette(p);
    }
    v->addWidget(m_list, 1);
    m_empty = new QLabel;
    m_empty->setObjectName(QStringLiteral("emptyState"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->hide();
    v->addWidget(m_empty, 1);

    auto *split = new QSplitter;
    split->addWidget(m_nav);
    split->addWidget(pane);
    split->setStretchFactor(1, 1);
    split->setSizes({230, 700});
    split->setChildrenCollapsible(false);
    setCentralWidget(split);

    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("status"));
    m_undoButton = new QPushButton(QStringLiteral("Undo"));
    m_undoButton->setObjectName(QStringLiteral("undoButton"));
    m_undoButton->setFlat(true);
    m_undoButton->hide();
    statusBar()->addWidget(m_status, 1);
    statusBar()->addPermanentWidget(m_undoButton);
    connect(m_undoButton, &QPushButton::clicked, this, &MainWindow::undo);

    m_notifyTimer = new QTimer(this);
    m_notifyTimer->setSingleShot(true);
    connect(m_notifyTimer, &QTimer::timeout, this, [this] {
        m_undoButton->hide();
        updateStatus();
    });
    m_syncTimer = new QTimer(this);
    m_syncTimer->setSingleShot(true);
    m_syncTimer->setInterval(5000);
    connect(m_syncTimer, &QTimer::timeout, this, &MainWindow::runSyncCommand);
    // The date rolls over at midnight: Today / Upcoming / overdue change.
    m_midnight = new QTimer(this);
    m_midnight->setInterval(60000);
    connect(m_midnight, &QTimer::timeout, this, [this, day = QDate::currentDate()]() mutable {
        if (QDate::currentDate() != day) {
            day = QDate::currentDate();
            refresh();
        }
    });
    m_midnight->start();

    connect(m_quick, &QLineEdit::returnPressed, this, [this] {
        if (!m_quick->text().trimmed().isEmpty()) {
            addFromText(m_quick->text());
        }
    });
    connect(m_quick, &QLineEdit::textChanged, this, &MainWindow::updateQuickPreview);
    m_quick->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &t) {
        static View before;
        if (!t.trimmed().isEmpty()) {
            if (m_view.kind != ViewKind::Search) {
                before = m_view;
            }
            setView(View{ViewKind::Search, {}, t});
        } else if (m_view.kind == ViewKind::Search) {
            setView(before);
        }
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        if (it && !it->data(RoleIsHeader).toBool()) {
            editTask(it->data(RoleTaskId).toString());
        }
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &MainWindow::taskContextMenu);
    connect(m_delegate, &TaskDelegate::toggleRequested, this, &MainWindow::toggleComplete);
    connect(m_store, &Store::changed, this, &MainWindow::refresh);
    connect(m_store, &Store::error, this, [this](const QString &msg) { notify(msg, false); });

    buildMenus();
    QSettings s(AppSettings::filePath(), QSettings::IniFormat);
    if (!restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray())) {
        resize(1000, 680);
    }
    const int lastKind = s.value(QStringLiteral("window/view"), int(ViewKind::Today)).toInt();
    View start{static_cast<ViewKind>(std::clamp(lastKind, 0, int(ViewKind::Label))),
               s.value(QStringLiteral("window/viewId")).toString(), {}};
    if ((start.kind == ViewKind::Project && m_store->projectName(start.id).isEmpty())
        || (start.kind == ViewKind::Label && m_store->labelName(start.id).isEmpty())) {
        start = View{};
    }
    m_view = start;
    refreshNav(); // the sidebar is otherwise only built on the first change
    setView(start);
    updateStatus();
    QTimer::singleShot(0, this, [this] { m_quick->setFocus(); });
}

void MainWindow::buildMenus()
{
#if !defined(Q_OS_MACOS)
    // Keep the menu bar in the window. With a global-menu service running
    // (com.canonical.AppMenu.Registrar, e.g. a GNOME app-menu extension) Qt
    // would otherwise export the menus over D-Bus and hide the bar, leaving
    // no visible way to reach Settings.
    menuBar()->setNativeMenuBar(false);
#endif
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    QAction *newTask = file->addAction(QStringLiteral("&New Task"), this, [this] {
        m_quick->setFocus();
        m_quick->selectAll();
    });
    newTask->setShortcut(QKeySequence::New);
    file->addAction(QStringLiteral("New &Project…"), this, &MainWindow::addProject);
    file->addSeparator();
    QAction *sync = file->addAction(QStringLiteral("&Sync Now"), this, &MainWindow::runSyncCommand);
    sync->setShortcuts({QKeySequence(Qt::Key_F5), QKeySequence(QStringLiteral("Ctrl+R"))});
    file->addSeparator();
    QAction *quit = file->addAction(QStringLiteral("&Quit"), qApp, &QApplication::closeAllWindows);
    quit->setShortcut(QKeySequence::Quit);
    quit->setMenuRole(QAction::QuitRole);

    QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    QAction *undoAct = edit->addAction(QStringLiteral("&Undo"), this, &MainWindow::undo);
    undoAct->setShortcut(QKeySequence::Undo);
    connect(edit, &QMenu::aboutToShow, this, [this, undoAct] {
        undoAct->setEnabled(canUndo());
        undoAct->setText(canUndo() ? QStringLiteral("&Undo %1").arg(m_undo.last().text) : QStringLiteral("&Undo"));
    });
    QAction *find = edit->addAction(QStringLiteral("&Find"), this, [this] {
        m_search->setFocus();
        m_search->selectAll();
    });
    find->setShortcut(QKeySequence::Find);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    struct Item {
        const char *name;
        ViewKind kind;
        const char *key;
    };
    for (const Item &it : {Item{"&Inbox", ViewKind::Inbox, "Ctrl+1"}, Item{"&Today", ViewKind::Today, "Ctrl+2"},
                           Item{"&Upcoming", ViewKind::Upcoming, "Ctrl+3"},
                           Item{"&Completed", ViewKind::Completed, "Ctrl+4"}}) {
        const ViewKind k = it.kind;
        QAction *a = viewMenu->addAction(QString::fromLatin1(it.name), this, [this, k] {
            m_search->clear();
            setView(View{k, {}, {}});
        });
        a->setShortcut(QKeySequence(QString::fromLatin1(it.key)));
    }

    QMenu *settings = menuBar()->addMenu(QStringLiteral("&Settings"));
    settings->setObjectName(QStringLiteral("settingsMenu"));
    QAction *prefs = settings->addAction(QStringLiteral("&Preferences…"), this, &MainWindow::openPreferences);
    prefs->setObjectName(QStringLiteral("preferencesAction"));
    // QKeySequence::Preferences is Cmd+, on macOS but unbound on Linux.
    prefs->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    prefs->setMenuRole(QAction::PreferencesRole);
    settings->addAction(QStringLiteral("Change &Data Folder…"), this, &MainWindow::openPreferences);
    settings->addAction(QStringLiteral("&Sync Command…"), this, &MainWindow::openPreferences);
    settings->addSeparator();
    QAction *autoUpdate = settings->addAction(QStringLiteral("Check for &Updates at Startup"));
    autoUpdate->setObjectName(QStringLiteral("autoUpdateAction"));
    autoUpdate->setCheckable(true);
    connect(settings, &QMenu::aboutToShow, this,
            [autoUpdate] { autoUpdate->setChecked(AppSettings::load().checkForUpdatesOnStartup); });
    connect(autoUpdate, &QAction::toggled, this, [](bool on) {
        AppSettings s = AppSettings::load();
        if (s.checkForUpdatesOnStartup != on) {
            s.checkForUpdatesOnStartup = on;
            s.save();
        }
    });
    settings->addSeparator();
    settings->addAction(QStringLiteral("Open Data &Folder"), this,
                        [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_store->dir())); });
    settings->addAction(QStringLiteral("Open Settings &File Location"), this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(AppSettings::filePath()).absolutePath()));
    });

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("&Quick Add Syntax"), this, [this] {
        QMessageBox::information(
            this, QStringLiteral("Quick Add"),
            QStringLiteral(
                "<p>Type a task and press Enter. Recognised words are taken out of the title:</p>"
                "<table cellspacing=4>"
                "<tr><td><b>Dates</b></td><td>today, tomorrow, fri, next mon, next week, in 3 days, nov 20, 12/24, eom</td></tr>"
                "<tr><td><b>Times</b></td><td>5pm, 9:30am, at 17:00, noon</td></tr>"
                "<tr><td><b>Repeat</b></td><td>every day, every weekday, every mon and thu, every 2 weeks, every 15th, "
                "monthly; <i>every!</i> counts from when you complete it</td></tr>"
                "<tr><td><b>Priority</b></td><td>p1 (red) … p4</td></tr>"
                "<tr><td><b>Project</b></td><td>#Work, #Home_Improvement (created if new)</td></tr>"
                "<tr><td><b>Labels</b></td><td>@phone @errand (created if new)</td></tr>"
                "</table>"
                "<p>Keys: <b>Q</b> or Ctrl+N quick add · <b>Space</b> complete · <b>Enter</b> edit · "
                "<b>Del</b> delete · <b>Alt+↑/↓</b> reorder · <b>Ctrl+Z</b> undo · "
                "<b>Ctrl+1…4</b> views · <b>F5</b> sync</p>"));
    });
    QAction *upd = help->addAction(QStringLiteral("Check for &Updates…"), this,
                                   [this] { UpdateManager::instance().checkNow(this); });
    upd->setMenuRole(QAction::ApplicationSpecificRole);
    QAction *ab = help->addAction(QStringLiteral("&About ztodo"), this, &MainWindow::about);
    ab->setMenuRole(QAction::AboutRole);
}

void MainWindow::setView(const View &v)
{
    m_view = v;
    selectNav(v);
    refreshList();
}

void MainWindow::selectNav(const View &v)
{
    const bool was = m_refreshing;
    m_refreshing = true;
    m_nav->setCurrentItem(nullptr);
    for (int i = 0; i < m_nav->count(); ++i) {
        QListWidgetItem *it = m_nav->item(i);
        if (it->data(RoleNavKind).toInt() == int(v.kind) && it->data(RoleNavId).toString() == v.id) {
            m_nav->setCurrentItem(it);
            break;
        }
    }
    m_refreshing = was;
}

void MainWindow::refresh()
{
    refreshNav();
    refreshList();
}

void MainWindow::refreshNav()
{
    const QList<Task> tasks = m_store->tasks();
    const QDate today = QDate::currentDate();
    const int scroll = m_nav->verticalScrollBar()->value();
    m_refreshing = true;
    m_nav->clear();
    auto add = [this, &tasks, &today](const QString &name, ViewKind kind, const QString &id, const QColor &color) {
        auto *it = new QListWidgetItem(name, m_nav);
        it->setData(RoleNavKind, int(kind));
        it->setData(RoleNavId, id);
        it->setData(RoleCount, countFor(tasks, View{kind, id, {}}, today));
        if (color.isValid()) {
            it->setData(RoleColor, color);
        }
        return it;
    };
    auto header = [this](const QString &name) {
        auto *it = new QListWidgetItem(name, m_nav);
        it->setData(RoleNavKind, -1);
        it->setFlags(Qt::NoItemFlags);
    };
    add(QStringLiteral("Inbox"), ViewKind::Inbox, {}, theme::priority(3));
    add(QStringLiteral("Today"), ViewKind::Today, {}, theme::today());
    add(QStringLiteral("Upcoming"), ViewKind::Upcoming, {}, theme::thisWeek());
    add(QStringLiteral("Completed"), ViewKind::Completed, {}, theme::muted());
    header(QStringLiteral("Projects"));
    for (const Project &p : m_store->projects()) {
        add(p.name, ViewKind::Project, p.id, QColor(p.color));
    }
    auto *addRow = new QListWidgetItem(QStringLiteral("Add project"), m_nav);
    addRow->setData(RoleNavKind, -2);
    const QList<Label> labels = m_store->labels();
    if (!labels.isEmpty()) {
        header(QStringLiteral("Labels"));
        for (const Label &l : labels) {
            add(l.name, ViewKind::Label, l.id, QColor(l.color));
        }
    }
    m_refreshing = false;
    selectNav(m_view);
    m_nav->verticalScrollBar()->setValue(scroll);
}

void MainWindow::refreshList()
{
    if (m_view.kind == ViewKind::Project && m_store->projectName(m_view.id).isEmpty()) {
        m_view = View{ViewKind::Inbox, {}, {}}; // deleted (maybe on another device)
        selectNav(m_view);
    } else if (m_view.kind == ViewKind::Label && m_store->labelName(m_view.id).isEmpty()) {
        m_view = View{ViewKind::Today, {}, {}};
        selectNav(m_view);
    }
    const QString selected = currentTaskId();
    const int scroll = m_list->verticalScrollBar()->value();
    const QDate today = QDate::currentDate();

    QString title;
    QString empty;
    switch (m_view.kind) {
    case ViewKind::Inbox:
        title = QStringLiteral("Inbox");
        empty = QStringLiteral("Your Inbox is empty.\nType a task above and press Enter.");
        break;
    case ViewKind::Today:
        title = QStringLiteral("Today");
        empty = QStringLiteral("Nothing due today. Enjoy your day!");
        break;
    case ViewKind::Upcoming:
        title = QStringLiteral("Upcoming");
        empty = QStringLiteral("Nothing scheduled.\nAdd dates with words like “fri” or “nov 20”.");
        break;
    case ViewKind::Completed:
        title = QStringLiteral("Completed");
        empty = QStringLiteral("No completed tasks yet.");
        break;
    case ViewKind::Project:
        title = m_store->projectName(m_view.id);
        empty = QStringLiteral("No open tasks in this project.");
        break;
    case ViewKind::Label:
        title = QLatin1Char('@') + m_store->labelName(m_view.id);
        empty = QStringLiteral("No open tasks with this label.");
        break;
    case ViewKind::Search:
        title = QStringLiteral("Search");
        empty = QStringLiteral("No tasks match “%1”.").arg(m_view.text.trimmed());
        break;
    }
    m_title->setText(title);
    setWindowTitle(windowTitleFor(title));
    m_empty->setText(empty);
    m_delegate->setShowProject(m_view.kind != ViewKind::Project && m_view.kind != ViewKind::Inbox);
    m_quick->setEnabled(m_view.kind != ViewKind::Completed);

    m_list->setUpdatesEnabled(false);
    m_list->clear();
    QListWidgetItem *toSelect = nullptr;
    for (const Row &r : buildRows(m_store->tasks(), m_view, today)) {
        auto *it = new QListWidgetItem(m_list);
        if (r.header) {
            it->setData(Qt::DisplayRole, r.headerText);
            it->setData(RoleIsHeader, true);
            it->setData(RoleOverdueHeader, r.overdueHeader);
            it->setFlags(Qt::NoItemFlags);
        } else {
            it->setData(RoleTaskId, r.task.id);
            it->setData(RoleDepth, r.depth);
            it->setData(Qt::AccessibleTextRole, r.task.title);
            it->setToolTip(r.task.notes.left(500));
            if (r.task.id == selected) {
                toSelect = it;
            }
        }
    }
    if (toSelect) {
        m_list->setCurrentItem(toSelect);
    }
    m_list->verticalScrollBar()->setValue(scroll);
    m_list->setUpdatesEnabled(true);
    const bool none = m_list->count() == 0;
    m_list->setVisible(!none);
    m_empty->setVisible(none);
    updateStatus();
}

QString MainWindow::currentTaskId() const
{
    QListWidgetItem *it = m_list->currentItem();
    return it && !it->data(RoleIsHeader).toBool() ? it->data(RoleTaskId).toString() : QString();
}

void MainWindow::updateQuickPreview()
{
    const QString text = m_quick->text();
    if (text.trimmed().isEmpty()) {
        m_quickPreview->hide();
        return;
    }
    const QuickAdd q = QuickAdd::parse(text);
    QStringList parts;
    auto chip = [&parts](const QString &s, const QColor &c) {
        parts << QStringLiteral("<span style=\"color:%1\">%2</span>").arg(c.name(), s.toHtmlEscaped());
    };
    if (q.due.isValid()) {
        chip(formatDue(q.due, q.time, QDate::currentDate()), theme::today());
    }
    if (!q.recur.isEmpty()) {
        chip(QStringLiteral("↻ ") + q.recur, theme::thisWeek());
    }
    if (q.priority < 4) {
        chip(QStringLiteral("P%1").arg(q.priority), theme::priority(q.priority));
    }
    if (!q.project.isEmpty()) {
        const bool exists = !m_store->findProject(q.project).isEmpty();
        chip(QStringLiteral("# ") + q.project + (exists ? QString() : QStringLiteral(" (new)")), theme::muted());
    }
    for (const QString &l : q.labels) {
        chip(QLatin1Char('@') + l + (m_store->findLabel(l).isEmpty() ? QStringLiteral(" (new)") : QString()),
             theme::muted());
    }
    if (parts.isEmpty()) {
        m_quickPreview->hide();
        return;
    }
    m_quickPreview->setText(QStringLiteral("<small><b>%1</b> &nbsp; %2</small>")
                                .arg(q.title.toHtmlEscaped(), parts.join(QStringLiteral(" &nbsp;·&nbsp; "))));
    m_quickPreview->show();
}

QString MainWindow::addFromText(const QString &text)
{
    const QDate today = QDate::currentDate();
    const QuickAdd q = QuickAdd::parse(text, today);
    Task t;
    t.title = q.title;
    t.dueDate = q.due;
    t.dueTime = q.time;
    t.recur = q.recur;
    t.priority = q.priority;
    QList<Change> changes;
    if (!q.project.isEmpty()) {
        t.project = m_store->findProject(q.project);
        if (t.project.isEmpty()) {
            t.project = m_store->addProject(q.project, theme::projectColor(int(m_store->projects().size())).name());
            changes << Change{kProject, t.project, {{QStringLiteral("deleted"), true}}};
        }
    } else if (m_view.kind == ViewKind::Project) {
        t.project = m_view.id;
    }
    for (const QString &name : q.labels) {
        QString id = m_store->findLabel(name);
        if (id.isEmpty()) {
            id = m_store->addLabel(name);
            changes << Change{kLabel, id, {{QStringLiteral("deleted"), true}}};
        }
        if (!id.isEmpty() && !t.labels.contains(id)) {
            t.labels << id;
        }
    }
    if (m_view.kind == ViewKind::Label && !t.labels.contains(m_view.id)) {
        t.labels << m_view.id;
    }
    if (m_view.kind == ViewKind::Today && !t.dueDate.isValid()) {
        t.dueDate = today;
    }
    const QString id = m_store->addTask(t);
    if (id.isEmpty()) {
        return {};
    }
    changes << Change{kTask, id, {{QStringLiteral("deleted"), true}}};
    pushUndo(QStringLiteral("Add Task"), changes);
    m_quick->clear();
    const QString where = t.project.isEmpty() ? QStringLiteral("Inbox") : m_store->projectName(t.project);
    QString msg = QStringLiteral("Added %1 to %2").arg(quoted(t.title), where);
    if (t.dueDate.isValid()) {
        msg += QStringLiteral(", due %1").arg(formatDue(t.dueDate, t.dueTime, today));
    }
    notify(msg, true);
    // Select it if it's in this view.
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(RoleTaskId).toString() == id) {
            m_list->setCurrentRow(i);
            m_list->scrollToItem(m_list->item(i));
            break;
        }
    }
    scheduleSync();
    return id;
}

void MainWindow::toggleComplete(const QString &taskId)
{
    Task t;
    if (!m_store->task(taskId, &t)) {
        return;
    }
    const QJsonObject before = m_store->complete(taskId);
    if (before.isEmpty()) {
        return;
    }
    Task after;
    m_store->task(taskId, &after);
    QString msg;
    if (!t.recur.isEmpty() && !t.done) {
        msg = QStringLiteral("Completed %1 — next: %2")
                  .arg(quoted(t.title), formatDue(after.dueDate, after.dueTime, QDate::currentDate()));
    } else {
        msg = after.done ? QStringLiteral("Completed %1").arg(quoted(t.title))
                         : QStringLiteral("Reopened %1").arg(quoted(t.title));
    }
    pushUndo(after.done || !t.recur.isEmpty() ? QStringLiteral("Complete") : QStringLiteral("Reopen"),
             {Change{kTask, taskId, before}});
    notify(msg, true);
    scheduleSync();
}

void MainWindow::editTask(const QString &id)
{
    Task t;
    if (!m_store->task(id, &t)) {
        return;
    }
    TaskDialog dlg(m_store, t, false, this);
    const int r = dlg.exec();
    if (r == TaskDialog::Deleted) {
        deleteTask(id);
        return;
    }
    if (r != QDialog::Accepted) {
        return;
    }
    const Task n = dlg.result();
    QJsonObject f;
    if (n.title != t.title) {
        f.insert(QStringLiteral("title"), n.title);
    }
    if (n.notes != t.notes) {
        f.insert(QStringLiteral("notes"), n.notes);
    }
    if (n.dueString() != t.dueString()) {
        f.insert(QStringLiteral("due"), n.dueString());
    }
    if (n.recur != t.recur) {
        f.insert(QStringLiteral("recur"), n.recur);
    }
    if (n.priority != t.priority) {
        f.insert(QStringLiteral("priority"), n.priority);
    }
    if (n.project != t.project) {
        f.insert(QStringLiteral("project"), n.project);
    }
    if (n.labels != t.labels) {
        f.insert(QStringLiteral("labels"), QJsonArray::fromStringList(n.labels));
    }
    if (f.isEmpty()) {
        return;
    }
    const QJsonObject before = m_store->snapshot(kTask, id, f.keys());
    if (m_store->setTaskFields(id, f)) {
        pushUndo(QStringLiteral("Edit"), {Change{kTask, id, before}});
        // Subtasks follow their parent to another project.
        if (f.contains(QStringLiteral("project"))) {
            QList<Change> sub;
            std::function<void(const QString &)> moveKids = [&](const QString &pid) {
                for (const Task &c : m_store->tasks()) {
                    if (c.parent == pid && c.project != n.project) {
                        sub << Change{kTask, c.id, m_store->snapshot(kTask, c.id, {QStringLiteral("project")})};
                        m_store->setTaskFields(c.id, {{QStringLiteral("project"), n.project}});
                        moveKids(c.id);
                    }
                }
            };
            moveKids(id);
            if (!sub.isEmpty()) {
                m_undo.last().changes << sub;
            }
        }
        notify(QStringLiteral("Saved %1").arg(quoted(n.title)), true);
        scheduleSync();
    }
}

void MainWindow::addSubtask(const QString &parentId)
{
    Task parent;
    if (!m_store->task(parentId, &parent)) {
        return;
    }
    Task t;
    t.parent = parentId;
    t.project = parent.project;
    TaskDialog dlg(m_store, t, true, this);
    dlg.setWindowTitle(QStringLiteral("New Subtask of %1").arg(quoted(parent.title)));
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    Task n = dlg.result();
    n.parent = parentId;
    // Right after the parent (and its other subtasks) in manual order.
    double after = parent.order;
    for (const Task &c : m_store->tasks()) {
        if (c.parent == parentId) {
            after = std::max(after, c.order);
        }
    }
    double next = after + 1;
    for (const Task &c : m_store->tasks()) {
        if (c.order > after && c.order < next) {
            next = c.order;
        }
    }
    n.order = (after + next) / 2;
    const QString id = m_store->addTask(n);
    if (!id.isEmpty()) {
        pushUndo(QStringLiteral("Add Subtask"), {Change{kTask, id, {{QStringLiteral("deleted"), true}}}});
        notify(QStringLiteral("Added subtask %1").arg(quoted(n.title)), true);
        scheduleSync();
    }
}

void MainWindow::deleteTask(const QString &id)
{
    Task t;
    if (!m_store->task(id, &t)) {
        return;
    }
    QList<Change> changes;
    std::function<void(const QString &)> collect = [&](const QString &tid) {
        changes << Change{kTask, tid, {{QStringLiteral("deleted"), false}}};
        for (const Task &c : m_store->tasks()) {
            if (c.parent == tid) {
                collect(c.id);
            }
        }
    };
    collect(id);
    m_store->remove(kTask, id);
    pushUndo(QStringLiteral("Delete"), changes);
    notify(QStringLiteral("Deleted %1").arg(quoted(t.title)), true);
    scheduleSync();
}

void MainWindow::moveTask(const QString &id, int delta)
{
    if (!m_view.isManualOrder()) {
        notify(QStringLiteral("Reorder tasks in the Inbox or a project."), false);
        return;
    }
    Task t;
    if (!m_store->task(id, &t)) {
        return;
    }
    // Siblings as shown: same parent (as displayed: same depth run).
    QList<Task> sib;
    for (const Row &r : buildRows(m_store->tasks(), m_view, QDate::currentDate())) {
        if (!r.header && r.task.parent == t.parent) {
            sib << r.task;
        }
    }
    qsizetype i = -1;
    for (qsizetype k = 0; k < sib.size(); ++k) {
        if (sib[k].id == id) {
            i = k;
        }
    }
    const qsizetype j = i + delta;
    if (i < 0 || j < 0 || j >= sib.size()) {
        return;
    }
    const Task &o = sib[j];
    double a = o.order;
    double b = t.order;
    if (a == b) {
        a += delta > 0 ? 0.001 : -0.001;
    }
    const QList<Change> changes{Change{kTask, id, m_store->snapshot(kTask, id, {QStringLiteral("order")})},
                                Change{kTask, o.id, m_store->snapshot(kTask, o.id, {QStringLiteral("order")})}};
    m_store->setTaskFields(id, {{QStringLiteral("order"), a}});
    m_store->setTaskFields(o.id, {{QStringLiteral("order"), b}});
    pushUndo(QStringLiteral("Move"), changes);
    scheduleSync();
}

void MainWindow::setTaskField(const QString &id, const QString &field, const QJsonValue &value, const QString &undoText)
{
    const QJsonObject before = m_store->snapshot(kTask, id, {field});
    if (m_store->setTaskFields(id, {{field, value}})) {
        pushUndo(undoText, {Change{kTask, id, before}});
        scheduleSync();
    }
}

void MainWindow::taskContextMenu(const QPoint &pos)
{
    QListWidgetItem *it = m_list->itemAt(pos);
    if (!it || it->data(RoleIsHeader).toBool()) {
        return;
    }
    m_list->setCurrentItem(it);
    const QString id = it->data(RoleTaskId).toString();
    Task t;
    if (!m_store->task(id, &t)) {
        return;
    }
    QMenu menu(this);
    menu.addAction(QStringLiteral("&Edit…"), this, [this, id] { editTask(id); });
    menu.addAction(t.done ? QStringLiteral("&Reopen") : QStringLiteral("&Complete"), this,
                   [this, id] { toggleComplete(id); });
    menu.addAction(QStringLiteral("Add &Subtask…"), this, [this, id] { addSubtask(id); });
    menu.addSeparator();

    QMenu *due = menu.addMenu(QStringLiteral("&Due"));
    const QDate today = QDate::currentDate();
    auto setDue = [this, id, t](const QDate &d) {
        Task n = t;
        n.dueDate = d;
        if (!d.isValid()) {
            n.dueTime = QTime();
        }
        setTaskField(id, QStringLiteral("due"), n.dueString(), QStringLiteral("Change Date"));
    };
    due->addAction(QStringLiteral("&Today"), this, [setDue, today] { setDue(today); });
    due->addAction(QStringLiteral("To&morrow"), this, [setDue, today] { setDue(today.addDays(1)); });
    due->addAction(QStringLiteral("Next &Week"), this, [setDue, today] { setDue(today.addDays(8 - today.dayOfWeek())); });
    due->addAction(QStringLiteral("&No Date"), this, [setDue] { setDue(QDate()); });

    QMenu *prio = menu.addMenu(QStringLiteral("&Priority"));
    for (int p = 1; p <= 4; ++p) {
        QAction *a = prio->addAction(QStringLiteral("Priority &%1").arg(p), this,
                                     [this, id, p] { setTaskField(id, QStringLiteral("priority"), p, QStringLiteral("Priority")); });
        a->setCheckable(true);
        a->setChecked(t.priority == p);
    }

    QMenu *move = menu.addMenu(QStringLiteral("&Move To"));
    auto moveTo = [this, id](const QString &pid) {
        if (Task cur; m_store->task(id, &cur) && cur.project != pid) {
            setTaskField(id, QStringLiteral("project"), pid, QStringLiteral("Move"));
            if (!cur.parent.isEmpty()) {
                // Out of its parent's project: no longer a subtask there.
                if (Task par; m_store->task(cur.parent, &par) && par.project != pid) {
                    m_undo.last().changes << Change{kTask, id, m_store->snapshot(kTask, id, {QStringLiteral("parent")})};
                    m_store->setTaskFields(id, {{QStringLiteral("parent"), QString()}});
                }
            }
        }
    };
    move->addAction(QStringLiteral("Inbox"), this, [moveTo] { moveTo(QString()); });
    for (const Project &p : m_store->projects()) {
        const QString pid = p.id;
        move->addAction(p.name, this, [moveTo, pid] { moveTo(pid); });
    }
    if (m_view.isManualOrder()) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Move &Up\tAlt+Up"), this, [this, id] { moveTask(id, -1); });
        menu.addAction(QStringLiteral("Move Do&wn\tAlt+Down"), this, [this, id] { moveTask(id, +1); });
    }
    menu.addSeparator();
    menu.addAction(QStringLiteral("De&lete"), this, [this, id] { deleteTask(id); });
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void MainWindow::navContextMenu(const QPoint &pos)
{
    QListWidgetItem *it = m_nav->itemAt(pos);
    QMenu menu(this);
    const int kind = it ? it->data(RoleNavKind).toInt() : -1;
    const QString id = it ? it->data(RoleNavId).toString() : QString();
    if (kind == int(ViewKind::Project)) {
        menu.addAction(QStringLiteral("&Rename…"), this, [this, id] { renameEntity(kProject, id); });
        QMenu *color = menu.addMenu(QStringLiteral("&Color"));
        for (int i = 0; i < 10; ++i) {
            const QColor c = theme::projectColor(i);
            QPixmap pm(14, 14);
            pm.fill(c);
            color->addAction(QIcon(pm), c.name(), this, [this, id, c] {
                const QJsonObject before = m_store->snapshot(kProject, id, {QStringLiteral("color")});
                m_store->put(kProject, id, {{QStringLiteral("color"), c.name()}});
                pushUndo(QStringLiteral("Color"), {Change{kProject, id, before}});
                scheduleSync();
            });
        }
        menu.addAction(QStringLiteral("&Delete Project…"), this, [this, id] { deleteProject(id); });
        menu.addSeparator();
    } else if (kind == int(ViewKind::Label)) {
        menu.addAction(QStringLiteral("&Rename…"), this, [this, id] { renameEntity(kLabel, id); });
        menu.addAction(QStringLiteral("&Delete Label"), this, [this, id] { deleteLabel(id); });
        menu.addSeparator();
    }
    menu.addAction(QStringLiteral("&Add Project…"), this, &MainWindow::addProject);
    menu.exec(m_nav->viewport()->mapToGlobal(pos));
}

void MainWindow::addProject()
{
    const View back = m_view;
    selectNav(back);
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, QStringLiteral("New Project"), QStringLiteral("Project name:"), QLineEdit::Normal, {}, &ok)
            .trimmed();
    if (!ok || name.isEmpty()) {
        selectNav(back);
        return;
    }
    if (const QString existing = m_store->findProject(name); !existing.isEmpty()) {
        setView(View{ViewKind::Project, existing, {}});
        return;
    }
    const QString id = m_store->addProject(name, theme::projectColor(int(m_store->projects().size())).name());
    if (!id.isEmpty()) {
        pushUndo(QStringLiteral("Add Project"), {Change{kProject, id, {{QStringLiteral("deleted"), true}}}});
        setView(View{ViewKind::Project, id, {}});
        scheduleSync();
    }
}

void MainWindow::renameEntity(const QString &kind, const QString &id)
{
    const QString old = kind == kProject ? m_store->projectName(id) : m_store->labelName(id);
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("Name:"),
                                               QLineEdit::Normal, old, &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == old) {
        return;
    }
    const QJsonObject before = m_store->snapshot(kind, id, {QStringLiteral("name")});
    m_store->put(kind, id, {{QStringLiteral("name"), name}});
    pushUndo(QStringLiteral("Rename"), {Change{kind, id, before}});
    scheduleSync();
}

void MainWindow::deleteProject(const QString &id)
{
    QStringList ids;
    for (const Task &t : m_store->tasks()) {
        if (t.project == id) {
            ids << t.id;
        }
    }
    const QString name = m_store->projectName(id);
    if (QMessageBox::question(this, QStringLiteral("Delete Project"),
                              ids.isEmpty() ? QStringLiteral("Delete the project %1?").arg(quoted(name))
                                            : QStringLiteral("Delete the project %1 and its %2 task(s)?")
                                                  .arg(quoted(name))
                                                  .arg(ids.size()))
        != QMessageBox::Yes) {
        return;
    }
    QList<Change> changes{Change{kProject, id, {{QStringLiteral("deleted"), false}}}};
    for (const QString &tid : ids) {
        changes << Change{kTask, tid, {{QStringLiteral("deleted"), false}}};
        m_store->put(kTask, tid, {{QStringLiteral("deleted"), true}});
    }
    m_store->remove(kProject, id);
    pushUndo(QStringLiteral("Delete Project"), changes);
    notify(QStringLiteral("Deleted project %1").arg(quoted(name)), true);
    scheduleSync();
}

void MainWindow::deleteLabel(const QString &id)
{
    const QString name = m_store->labelName(id);
    m_store->remove(kLabel, id);
    pushUndo(QStringLiteral("Delete Label"), {Change{kLabel, id, {{QStringLiteral("deleted"), false}}}});
    notify(QStringLiteral("Deleted label @%1 (tasks keep their other labels)").arg(name), true);
    scheduleSync();
}

void MainWindow::pushUndo(const QString &text, const QList<Change> &changes)
{
    m_undo << UndoEntry{text, changes};
    while (m_undo.size() > 100) {
        m_undo.removeFirst();
    }
}

void MainWindow::undo()
{
    if (m_undo.isEmpty()) {
        notify(QStringLiteral("Nothing to undo."), false);
        return;
    }
    const UndoEntry e = m_undo.takeLast();
    // Reverse order: the last change made is undone first.
    for (qsizetype i = e.changes.size() - 1; i >= 0; --i) {
        const Change &c = e.changes.at(i);
        m_store->put(c.kind, c.id, c.before);
    }
    m_undoButton->hide();
    notify(QStringLiteral("Undid %1").arg(e.text.toLower()), false);
    scheduleSync();
}

void MainWindow::notify(const QString &text, bool offerUndo)
{
    m_status->setText(text);
    m_undoButton->setVisible(offerUndo && canUndo());
    m_notifyTimer->start(offerUndo ? 8000 : 5000);
}

void MainWindow::updateStatus()
{
    if (m_notifyTimer->isActive()) {
        return;
    }
    QString dir = m_store->dir();
    const QString home = QDir::homePath();
    if (dir.startsWith(home)) {
        dir = QLatin1Char('~') + dir.mid(home.size());
    }
    int open = 0;
    for (const Task &t : m_store->tasks()) {
        open += t.done ? 0 : 1;
    }
    QString s = QStringLiteral("%1 open task%2 · %3").arg(open).arg(open == 1 ? "" : "s").arg(dir);
    if (m_lastSyncRun.isValid()) {
        s += QStringLiteral(" · sync started %1").arg(m_lastSyncRun.toString(QStringLiteral("HH:mm")));
    }
    m_status->setText(s);
}

void MainWindow::scheduleSync()
{
    m_syncTimer->start();
}

void MainWindow::runSyncCommand()
{
    m_syncTimer->stop();
    const QString cmd = AppSettings::load().syncCommand.trimmed();
    if (!cmd.isEmpty()) {
        QStringList args = QProcess::splitCommand(cmd);
        const QString prog = args.isEmpty() ? QString() : args.takeFirst();
        if (prog.isEmpty() || !QProcess::startDetached(prog, args)) {
            notify(QStringLiteral("Couldn't run the sync command: %1").arg(cmd), false);
        } else {
            m_lastSyncRun = QDateTime::currentDateTime();
        }
    }
    // Pick up what the sync (or Box Drive) brings in, now and a bit later.
    for (int ms : {0, 4000, 15000, 45000}) {
        QTimer::singleShot(ms, this, [this] {
            if (m_store->reload()) {
                refresh();
            }
        });
    }
    updateStatus();
}

void MainWindow::openPreferences()
{
    const AppSettings cur = AppSettings::load();
    SettingsDialog dlg(cur, false, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const AppSettings s = dlg.result();
    if (s.dataDir != cur.dataDir) {
        QString err;
        if (!m_store->open(s.dataDir, deviceId(), &err)) {
            QMessageBox::warning(this, QStringLiteral("Data Folder"), err);
            m_store->open(cur.dataDir, deviceId());
            return;
        }
        m_undo.clear();
    }
    s.save();
    refresh();
}

void MainWindow::about()
{
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("About ztodo"));
    box.setIconPixmap(windowIcon().pixmap(96, 96));
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral("<h3>ztodo %1</h3>"
                               "<p>A Todoist-style task manager for macOS and Linux. Your tasks live in a Box "
                               "folder shared by all your devices.</p>"
                               "<p>Data folder: <tt>%2</tt><br>This device: <tt>%3</tt></p>"
                               "<p><a href=\"%4\">%4</a><br>MIT License © 2026 Stephen B. Johnson</p>")
                    .arg(QString::fromLatin1(kVersionString), m_store->dir().toHtmlEscaped(),
                         m_store->deviceId().toHtmlEscaped(), QString::fromLatin1(kRepoUrl)));
    box.exec();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *e)
{
    if (e->type() != QEvent::KeyPress) {
        return QMainWindow::eventFilter(obj, e);
    }
    auto *ke = static_cast<QKeyEvent *>(e);
    if (obj == m_quick) {
        if (ke->key() == Qt::Key_Escape) {
            m_quick->clear();
            m_list->setFocus();
            return true;
        }
        if (ke->key() == Qt::Key_Down) {
            m_list->setFocus();
            if (!m_list->currentItem()) {
                for (int i = 0; i < m_list->count(); ++i) {
                    if (!m_list->item(i)->data(RoleIsHeader).toBool()) {
                        m_list->setCurrentRow(i);
                        break;
                    }
                }
            }
            return true;
        }
        return QMainWindow::eventFilter(obj, e);
    }
    if (obj == m_list) {
        const QString id = currentTaskId();
        const auto mods = ke->modifiers() & ~Qt::KeypadModifier;
        if (ke->key() == Qt::Key_Q && mods == Qt::NoModifier) {
            m_quick->setFocus();
            return true;
        }
        if (id.isEmpty()) {
            return QMainWindow::eventFilter(obj, e);
        }
        if (ke->key() == Qt::Key_Space && mods == Qt::NoModifier) {
            toggleComplete(id);
            return true;
        }
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && mods == Qt::NoModifier) {
            editTask(id);
            return true;
        }
        if ((ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) && mods == Qt::NoModifier) {
            deleteTask(id);
            return true;
        }
        if ((ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down) && mods == Qt::AltModifier) {
            moveTask(id, ke->key() == Qt::Key_Up ? -1 : 1);
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, e);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    QSettings s(AppSettings::filePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    if (m_view.kind != ViewKind::Search) {
        s.setValue(QStringLiteral("window/view"), int(m_view.kind));
        s.setValue(QStringLiteral("window/viewId"), m_view.id);
    }
    s.sync();
    if (m_syncTimer->isActive()) {
        runSyncCommand(); // don't leave a change unsynced
    }
    e->accept();
}

} // namespace ztodo
