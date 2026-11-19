#include "Delegates.hpp"

#include "DateText.hpp"
#include "Store.hpp"
#include "Theme.hpp"
#include "Views.hpp"

#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace ztodo {

namespace {
constexpr int kIndent = 26;
constexpr int kLeft = 14;

// Row padding chosen so Medium (the platform default font) keeps the original
// 40 / 58 px task rows and 34 / 38 px sidebar rows at a typical 16 px line.
int taskChrome(const QFontMetrics &fm)
{
    return std::max(16, int(std::lround(fm.height() * 1.5)));
}

int circleSize(const QFontMetrics &fm)
{
    return std::max(14, fm.height() + 2);
}

QRect circleRect(const QRect &row, int depth, const QFontMetrics &fm)
{
    const int cs = circleSize(fm);
    const int x = row.left() + kLeft + depth * kIndent;
    const int titleBand = fm.height() + taskChrome(fm) / 2;
    const int y = row.top() + std::max(2, (titleBand - cs) / 2);
    return QRect(x, y, cs, cs);
}

QFont metaFont(const QFont &base)
{
    QFont mf = base;
    mf.setPointSizeF(std::max(7.0, base.pointSizeF() > 0 ? base.pointSizeF() * 0.86 : 8.0));
    return mf;
}

QColor dueColor(const Task &t, const QDate &today)
{
    if (t.done) {
        return theme::muted();
    }
    const qint64 d = today.daysTo(t.dueDate);
    if (d < 0) {
        return theme::overdue();
    }
    if (d == 0) {
        return theme::today();
    }
    if (d == 1) {
        return theme::tomorrow();
    }
    if (d < 7) {
        return theme::thisWeek();
    }
    return theme::muted();
}

void drawCheck(QPainter *p, const QRectF &r, const QColor &c, qreal width)
{
    QPainterPath path;
    path.moveTo(r.left() + r.width() * 0.28, r.top() + r.height() * 0.52);
    path.lineTo(r.left() + r.width() * 0.44, r.top() + r.height() * 0.68);
    path.lineTo(r.left() + r.width() * 0.74, r.top() + r.height() * 0.36);
    p->setPen(QPen(c, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path);
}
} // namespace

TaskDelegate::TaskDelegate(Store *store, QObject *parent)
    : QStyledItemDelegate(parent)
    , m_store(store)
{
}

QSize TaskDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    const QFontMetrics fm(opt.font);
    const int chrome = taskChrome(fm);
    if (index.data(RoleIsHeader).toBool()) {
        return QSize(opt.rect.width(), fm.height() + chrome);
    }
    Task t;
    if (!m_store->task(index.data(RoleTaskId).toString(), &t)) {
        return QSize(opt.rect.width(), fm.height() + chrome);
    }
    const bool meta = t.hasDue() || !t.labels.isEmpty() || !t.notes.isEmpty() || m_showProject;
    if (!meta) {
        return QSize(opt.rect.width(), fm.height() + chrome);
    }
    return QSize(opt.rect.width(), fm.height() + QFontMetrics(metaFont(opt.font)).height() + chrome + 2);
}

void TaskDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRect r = opt.rect;
    // Headers are disabled items (not selectable): use the active text colour,
    // not the greyed-out one Qt passes for them.
    const QColor text = (opt.widget ? opt.widget->palette() : opt.palette).color(QPalette::Active, QPalette::Text);

    if (index.data(RoleIsHeader).toBool()) {
        QFont f = opt.font;
        f.setBold(true);
        p->setFont(f);
        p->setPen(index.data(RoleOverdueHeader).toBool() ? theme::overdue() : text);
        p->drawText(r.adjusted(kLeft, 12, -kLeft, 0), Qt::AlignLeft | Qt::AlignTop, index.data(Qt::DisplayRole).toString());
        p->setPen(theme::separator());
        p->drawLine(r.left() + kLeft, r.bottom(), r.right() - kLeft, r.bottom());
        p->restore();
        return;
    }

    Task t;
    if (!m_store->task(index.data(RoleTaskId).toString(), &t)) {
        p->restore();
        return;
    }
    const int depth = index.data(RoleDepth).toInt();
    if (opt.state & QStyle::State_Selected) {
        QColor sel = theme::accent();
        sel.setAlpha(theme::isDark() ? 60 : 38);
        p->fillRect(r, sel);
    } else if (opt.state & QStyle::State_MouseOver) {
        p->fillRect(r, theme::hover());
    }

    // Completion circle.
    const QFontMetrics titleFm(opt.font);
    const QRectF c = QRectF(circleRect(r, depth, titleFm)).adjusted(0.5, 0.5, -0.5, -0.5);
    const QColor pc = theme::priority(t.priority);
    QColor fill = pc;
    fill.setAlpha(t.priority < 4 ? 40 : 0);
    if (t.done) {
        p->setPen(Qt::NoPen);
        p->setBrush(pc);
        p->drawEllipse(c);
        drawCheck(p, c, opt.palette.color(QPalette::Base), 2.0);
    } else {
        p->setPen(QPen(pc, t.priority < 4 ? 2.0 : 1.4));
        p->setBrush(fill);
        p->drawEllipse(c);
        if (opt.state & QStyle::State_MouseOver) {
            const QPoint mouse = opt.widget ? opt.widget->mapFromGlobal(QCursor::pos()) : QPoint();
            if (c.adjusted(-4, -4, 4, 4).contains(mouse)) {
                drawCheck(p, c, pc, 1.6);
            }
        }
    }

    // Title.
    const int tx = int(c.right()) + 12;
    const int right = r.right() - kLeft;
    QFont tf = opt.font;
    tf.setStrikeOut(t.done);
    p->setFont(tf);
    p->setPen(t.done ? theme::muted() : text);
    const QFontMetrics fm(tf);
    const int titleY = r.top() + std::max(2, (taskChrome(fm) / 2 - 2));
    p->drawText(QRect(tx, titleY, right - tx, fm.height() + 2), Qt::AlignLeft | Qt::AlignVCenter,
                fm.elidedText(t.title, Qt::ElideRight, right - tx));

    // Meta line.
    const QFont mf = metaFont(opt.font);
    p->setFont(mf);
    const QFontMetrics mm(mf);
    const int my = titleY + fm.height() + 2;
    int x = tx;
    auto chip = [&](const QString &s, const QColor &col) {
        if (s.isEmpty() || x >= right) {
            return;
        }
        p->setPen(col);
        const int w = mm.horizontalAdvance(s);
        p->drawText(QRect(x, my, std::min(w, right - x) + 1, mm.height()), Qt::AlignLeft | Qt::AlignVCenter, s);
        x += w + 14;
    };
    const QDate today = QDate::currentDate();
    if (t.hasDue()) {
        QString due = formatDue(t.dueDate, t.dueTime, today);
        if (!t.recur.isEmpty()) {
            due += QStringLiteral("  ↻");
        }
        chip(due, dueColor(t, today));
    }
    if (!t.notes.isEmpty()) {
        chip(QStringLiteral("≡ notes"), theme::muted());
    }
    for (const QString &l : t.labels) {
        const QString name = m_store->labelName(l);
        if (!name.isEmpty()) {
            chip(QLatin1Char('@') + name, theme::muted());
        }
    }
    if (m_showProject) {
        const QString pname = t.project.isEmpty() ? QStringLiteral("Inbox") : m_store->projectName(t.project);
        const QString s = QStringLiteral("# ") + pname;
        const int w = mm.horizontalAdvance(s);
        if (right - w > x) {
            p->setPen(theme::muted());
            p->drawText(QRect(right - w, my, w + 1, mm.height()), Qt::AlignRight | Qt::AlignVCenter, s);
        }
    }

    p->setPen(theme::separator());
    p->drawLine(tx, r.bottom(), right, r.bottom());
    p->restore();
}

bool TaskDelegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &opt,
                               const QModelIndex &index)
{
    if (!index.data(RoleIsHeader).toBool()
        && (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonDblClick)) {
        auto *me = static_cast<QMouseEvent *>(event);
        const QRect hit = circleRect(opt.rect, index.data(RoleDepth).toInt(), QFontMetrics(opt.font)).adjusted(-6, -6, 6, 6);
        if (me->button() == Qt::LeftButton && hit.contains(me->position().toPoint())) {
            if (event->type() == QEvent::MouseButtonRelease) {
                emit toggleRequested(index.data(RoleTaskId).toString());
            }
            return true; // don't select / open on the circle
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, opt, index);
}

// --- sidebar --------------------------------------------------------------

QSize NavDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    const int kind = index.data(RoleNavKind).toInt();
    const int line = QFontMetrics(opt.font).height();
    // 16 px line -> 38 px section / 34 px row, the original fixed heights.
    return QSize(opt.rect.width(), kind == -1 ? line + line * 11 / 8 : line + line * 9 / 8);
}

void NavDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRect r = opt.rect.adjusted(6, 1, -6, -1);
    const int kind = index.data(RoleNavKind).toInt();
    const QString name = index.data(Qt::DisplayRole).toString();
    // Section headers are disabled items: use the active colours for all rows.
    const QColor text = (opt.widget ? opt.widget->palette() : opt.palette).color(QPalette::Active, QPalette::Text);
    const bool selected = opt.state & QStyle::State_Selected;

    if (kind == -1) { // section header
        QFont f = opt.font;
        f.setBold(true);
        f.setPointSizeF(opt.font.pointSizeF() * 0.9);
        p->setFont(f);
        p->setPen(theme::sidebarMuted());
        p->drawText(r.adjusted(10, 10, -10, 0), Qt::AlignLeft | Qt::AlignVCenter, name.toUpper());
        p->restore();
        return;
    }
    if (selected) {
        QColor sel = theme::accent();
        sel.setAlpha(theme::isDark() ? 115 : 80);
        p->setPen(Qt::NoPen);
        p->setBrush(sel);
        p->drawRoundedRect(r, 6, 6);
    } else if (opt.state & QStyle::State_MouseOver) {
        p->setPen(Qt::NoPen);
        p->setBrush(theme::hover());
        p->drawRoundedRect(r, 6, 6);
    }

    const int isz = std::max(14, QFontMetrics(opt.font).height());
    const QRectF icon(r.left() + 10, r.center().y() - isz / 2.0, isz, isz);
    QColor ic = index.data(RoleColor).value<QColor>();
    if (!ic.isValid() || theme::isDefaultLabelColor(ic)) {
        ic = theme::sidebarMuted();
    }
    p->setPen(QPen(ic, 1.6));
    p->setBrush(Qt::NoBrush);
    if (kind == -2) { // "+ Add ..."
        p->drawLine(QPointF(icon.center().x(), icon.top() + 3), QPointF(icon.center().x(), icon.bottom() - 3));
        p->drawLine(QPointF(icon.left() + 3, icon.center().y()), QPointF(icon.right() - 3, icon.center().y()));
    } else {
        switch (static_cast<ViewKind>(kind)) {
        case ViewKind::Inbox: {
            QPainterPath tray;
            tray.moveTo(icon.left() + 1, icon.top() + 9);
            tray.lineTo(icon.left() + 5, icon.top() + 9);
            tray.lineTo(icon.left() + 6.5, icon.top() + 11.5);
            tray.lineTo(icon.right() - 6.5, icon.top() + 11.5);
            tray.lineTo(icon.right() - 5, icon.top() + 9);
            tray.lineTo(icon.right() - 1, icon.top() + 9);
            p->drawRoundedRect(icon.adjusted(1, 2, -1, -1), 3, 3);
            p->drawPath(tray);
            break;
        }
        case ViewKind::Today: {
            p->drawRoundedRect(icon.adjusted(1, 1, -1, -1), 3, 3);
            QFont f = opt.font;
            f.setPixelSize(std::max(7, isz * 9 / 16));
            f.setBold(true);
            p->setFont(f);
            p->drawText(icon.adjusted(0, 2, 0, 0), Qt::AlignCenter, QString::number(QDate::currentDate().day()));
            break;
        }
        case ViewKind::Upcoming:
            p->drawRoundedRect(icon.adjusted(1, 1, -1, -1), 3, 3);
            p->drawLine(QPointF(icon.left() + 1, icon.top() + 5.5), QPointF(icon.right() - 1, icon.top() + 5.5));
            for (int i = 0; i < 3; ++i) {
                p->drawPoint(QPointF(icon.left() + 5 + i * 3, icon.top() + 10));
            }
            break;
        case ViewKind::Completed:
            p->drawEllipse(icon.adjusted(1, 1, -1, -1));
            drawCheck(p, icon, ic, 1.6);
            break;
        case ViewKind::Project:
            p->setPen(Qt::NoPen);
            p->setBrush(ic);
            p->drawEllipse(icon.center(), 5, 5);
            break;
        case ViewKind::Label: {
            QFont f = opt.font;
            f.setBold(true);
            p->setFont(f);
            p->drawText(icon, Qt::AlignCenter, QStringLiteral("@"));
            break;
        }
        case ViewKind::Search:
            break;
        }
    }

    QFont nf = opt.font;
    nf.setBold(selected);
    p->setFont(nf);
    p->setPen(kind == -2 ? theme::sidebarMuted() : text);
    const int count = index.data(RoleCount).toInt();
    const QString cs = count > 0 ? QString::number(count) : QString();
    const QFontMetrics fm(nf);
    const int cw = fm.horizontalAdvance(cs);
    const QRect textRect(int(icon.right()) + 10, r.top(), r.right() - int(icon.right()) - 20 - cw, r.height());
    p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(name, Qt::ElideRight, textRect.width()));
    if (!cs.isEmpty()) {
        p->setPen(selected ? text : theme::sidebarMuted());
        p->drawText(r.adjusted(0, 0, -10, 0), Qt::AlignRight | Qt::AlignVCenter, cs);
    }
    p->restore();
}

} // namespace ztodo
