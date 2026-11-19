#include "StatsDialog.hpp"

#include "Theme.hpp"

#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace ztodo {

namespace {
QString plural(int n, const char *one, const char *many)
{
    return QStringLiteral("%1 %2").arg(n).arg(QString::fromLatin1(n == 1 ? one : many));
}
} // namespace

DailyChart::DailyChart(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("dailyChart"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void DailyChart::setData(const QList<int> &perDay, const QDate &lastDay)
{
    m_counts = perDay;
    m_last = lastDay;
    QStringList parts;
    for (qsizetype i = 0; i < m_counts.size(); ++i) {
        parts << QStringLiteral("%1: %2")
                     .arg(QLocale().toString(m_last.addDays(i + 1 - m_counts.size()), QStringLiteral("ddd d MMM")))
                     .arg(m_counts.at(i));
    }
    setAccessibleDescription(parts.join(QStringLiteral(", ")));
    update();
}

QSize DailyChart::sizeHint() const
{
    return {420, fontMetrics().height() * 9};
}

QSize DailyChart::minimumSizeHint() const
{
    return {240, fontMetrics().height() * 6};
}

void DailyChart::paintEvent(QPaintEvent *)
{
    if (m_counts.isEmpty()) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QFontMetrics fm = fontMetrics();
    const int n = int(m_counts.size());
    const int labelH = fm.height() + 4;
    const int valueH = fm.height() + 2;
    const QRect plot = rect().adjusted(0, valueH, 0, -labelH);
    const int maxCount = std::max(1, *std::max_element(m_counts.cbegin(), m_counts.cend()));
    const double slot = double(plot.width()) / n;
    const double barW = std::max(4.0, slot * 0.62);

    // Baseline.
    p.setPen(QPen(theme::separator(), 1));
    p.drawLine(plot.bottomLeft(), plot.bottomRight());

    QColor faded = theme::accent();
    faded.setAlpha(theme::isDark() ? 150 : 120);
    for (int i = 0; i < n; ++i) {
        const int c = m_counts.at(i);
        const bool isToday = i == n - 1;
        const double cx = plot.left() + slot * (i + 0.5);
        const QDate day = m_last.addDays(i + 1 - n);
        if (c > 0) {
            const double h = std::max(3.0, plot.height() * double(c) / maxCount);
            const QRectF bar(cx - barW / 2, plot.bottom() - h, barW, h);
            p.setPen(Qt::NoPen);
            p.setBrush(isToday ? theme::accent() : faded);
            p.drawRoundedRect(bar, 3, 3);
            p.setPen(palette().color(QPalette::WindowText));
            p.drawText(QRectF(cx - slot / 2, bar.top() - valueH, slot, valueH), Qt::AlignHCenter | Qt::AlignBottom,
                       QString::number(c));
        }
        // Day of month under every bar; the weekday letter would crowd at 14 days.
        QFont f = font();
        f.setBold(isToday);
        p.setFont(f);
        p.setPen(isToday ? palette().color(QPalette::WindowText) : theme::muted());
        p.drawText(QRectF(cx - slot / 2, plot.bottom() + 4, slot, labelH - 4), Qt::AlignHCenter | Qt::AlignTop,
                   QString::number(day.day()));
        p.setFont(font());
    }
}

StatsDialog::StatsDialog(Store *store, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
{
    setObjectName(QStringLiteral("statsDialog"));
    setWindowTitle(QStringLiteral("Productivity"));
    auto *layout = new QVBoxLayout(this);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    m_today = tile(QStringLiteral("statToday"), QStringLiteral("Completed today"), &m_todaySub, grid, 0);
    m_week = tile(QStringLiteral("statWeek"), QStringLiteral("This week"), &m_weekSub, grid, 1);
    m_streak = tile(QStringLiteral("statStreak"), QStringLiteral("Day streak"), &m_streakSub, grid, 2);
    m_month = tile(QStringLiteral("stat30"), QStringLiteral("Last 30 days"), &m_monthSub, grid, 3);
    layout->addLayout(grid);

    auto *chartTitle = new QLabel(QStringLiteral("<b>Last 14 days</b>"));
    layout->addSpacing(8);
    layout->addWidget(chartTitle);
    m_chart = new DailyChart;
    layout->addWidget(m_chart, 1);

    m_open = new QLabel;
    m_open->setObjectName(QStringLiteral("statOpen"));
    layout->addSpacing(8);
    layout->addWidget(m_open);

    auto *projTitle = new QLabel(QStringLiteral("<b>By project, last 30 days</b>"));
    layout->addSpacing(8);
    layout->addWidget(projTitle);
    m_projects = new QLabel;
    m_projects->setObjectName(QStringLiteral("statProjects"));
    m_projects->setTextFormat(Qt::RichText);
    layout->addWidget(m_projects);

    m_footer = new QLabel;
    m_footer->setObjectName(QStringLiteral("statFooter"));
    m_footer->setWordWrap(true);
    layout->addSpacing(8);
    layout->addWidget(m_footer);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    connect(m_store, &Store::changed, this, [this] {
        if (isVisible()) {
            refresh();
        }
    });
    refresh();
}

QLabel *StatsDialog::tile(const QString &objectName, const QString &caption, QLabel **sub, QGridLayout *grid, int col)
{
    auto *frame = new QFrame;
    frame->setFrameShape(QFrame::StyledPanel);
    auto *v = new QVBoxLayout(frame);
    v->setSpacing(2);
    auto *value = new QLabel;
    value->setObjectName(objectName);
    QFont big = value->font();
    big.setPointSizeF(big.pointSizeF() * 2.0);
    big.setBold(true);
    value->setFont(big);
    auto *cap = new QLabel(caption);
    *sub = new QLabel;
    (*sub)->setObjectName(objectName + QStringLiteral("Sub"));
    m_muted << cap << *sub;
    v->addWidget(value);
    v->addWidget(cap);
    v->addWidget(*sub);
    grid->addWidget(frame, 0, col);
    grid->setColumnStretch(col, 1);
    return value;
}

void StatsDialog::showEvent(QShowEvent *e)
{
    refresh(); // the date may have changed while it was hidden
    QDialog::showEvent(e);
}

void StatsDialog::refresh()
{
    const QDate today = QDate::currentDate();
    const QList<Task> tasks = m_store->tasks();
    m_stats = computeStats(tasks, completionHistory(tasks, m_store->completions()), today);
    const Stats &s = m_stats;
    for (QLabel *l : std::as_const(m_muted)) { // re-applied: the theme can switch while open
        QPalette pal = l->palette();
        pal.setColor(QPalette::WindowText, theme::muted());
        l->setPalette(pal);
    }

    m_today->setText(QString::number(s.today));
    m_todaySub->setText(QStringLiteral("%1 still due today").arg(s.dueToday));
    m_week->setText(QString::number(s.thisWeek));
    m_weekSub->setText(QStringLiteral("%1 in the last 7 days").arg(s.last7));
    m_streak->setText(QString::number(s.currentStreak));
    m_streakSub->setText(QStringLiteral("Longest: %1").arg(plural(s.longestStreak, "day", "days")));
    m_month->setText(QString::number(s.last30));
    m_monthSub->setText(QStringLiteral("%1 a day on average").arg(QLocale().toString(s.avgPerDay30, 'f', 1)));

    m_chart->setData(s.daily, today);

    QString open = QStringLiteral("<b>Open:</b> %1").arg(plural(s.open, "task", "tasks"));
    if (s.overdue > 0) {
        open += QStringLiteral(" · <span style='color:%1'>%2 overdue</span>").arg(theme::overdue().name()).arg(s.overdue);
    }
    m_open->setText(open);

    if (s.byProject30.isEmpty()) {
        m_projects->setText(QStringLiteral("<span style='color:%1'>Nothing completed yet.</span>").arg(theme::muted().name()));
    } else {
        QHash<QString, QString> colors;
        for (const Project &p : m_store->projects()) {
            colors.insert(p.id, p.color);
        }
        QString html = QStringLiteral("<table cellspacing=0 cellpadding=2>");
        for (const ProjectCount &pc : s.byProject30) {
            QString name;
            QString color = theme::muted().name();
            if (pc.project.isEmpty()) {
                name = QStringLiteral("Inbox");
            } else {
                name = m_store->projectName(pc.project);
                if (name.isEmpty()) {
                    name = QStringLiteral("<i>deleted project</i>");
                } else {
                    name = name.toHtmlEscaped();
                    if (QColor(colors.value(pc.project)).isValid()) {
                        color = colors.value(pc.project);
                    }
                }
            }
            html += QStringLiteral("<tr><td><span style='color:%1'>●</span>&nbsp;%2</td>"
                                   "<td align=right>&nbsp;&nbsp;&nbsp;%3</td></tr>")
                        .arg(color, name)
                        .arg(pc.count);
        }
        html += QStringLiteral("</table>");
        m_projects->setText(html);
    }

    QStringList footer{QStringLiteral("%1 completed in all").arg(s.total)};
    if (s.bestDay.isValid()) {
        footer << QStringLiteral("best day %1 (%2)")
                      .arg(QLocale().toString(s.bestDay, QStringLiteral("ddd d MMM yyyy")))
                      .arg(s.bestDayCount);
    }
    m_footer->setText(footer.join(QStringLiteral(" · ")));
}

} // namespace ztodo
