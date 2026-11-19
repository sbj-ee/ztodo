#pragma once

#include "Stats.hpp"
#include "Store.hpp"

#include <QDialog>
#include <QWidget>

class QGridLayout;
class QLabel;

namespace ztodo {

// Completions per day as bars, today's highlighted.
class DailyChart : public QWidget {
    Q_OBJECT
public:
    explicit DailyChart(QWidget *parent = nullptr);
    void setData(const QList<int> &perDay, const QDate &lastDay);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    QList<int> m_counts;
    QDate m_last;
};

// View → Productivity: completions today / this week / last 30 days, streaks,
// a 14-day chart, completions by project and the open list. Non-modal; follows
// the Store live (including other devices' changes) and the date.
class StatsDialog : public QDialog {
    Q_OBJECT
public:
    explicit StatsDialog(Store *store, QWidget *parent = nullptr);

    void refresh();
    const Stats &stats() const { return m_stats; }

protected:
    void showEvent(QShowEvent *e) override;

private:
    QLabel *tile(const QString &objectName, const QString &caption, QLabel **sub, QGridLayout *grid, int col);

    Store *m_store;
    Stats m_stats;
    QLabel *m_today;
    QLabel *m_todaySub;
    QLabel *m_week;
    QLabel *m_weekSub;
    QLabel *m_streak;
    QLabel *m_streakSub;
    QLabel *m_month;
    QLabel *m_monthSub;
    DailyChart *m_chart;
    QLabel *m_open;
    QLabel *m_projects;
    QLabel *m_footer;
    QList<QLabel *> m_muted; // tile captions
};

} // namespace ztodo
