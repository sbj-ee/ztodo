// MainWindow: sidebar (views, projects, labels) and their management.
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
#include <QSystemTrayIcon>
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
    m_store->beginBatch();
    for (const QString &tid : ids) {
        changes << Change{kTask, tid, {{QStringLiteral("deleted"), false}}};
        m_store->put(kTask, tid, {{QStringLiteral("deleted"), true}});
    }
    m_store->remove(kProject, id);
    m_store->endBatch();
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

} // namespace ztodo
