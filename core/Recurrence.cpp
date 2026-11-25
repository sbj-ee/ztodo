#include "Recurrence.hpp"

#include "DateText.hpp"

#include <QRegularExpression>

#include <algorithm>

namespace ztodo {

QString recurrencePattern()
{
    const QString wd = weekdayPattern();
    const QString list = QStringLiteral("%1(?:\\s*(?:,\\s*and|,|and|&)\\s*%1)*").arg(wd);
    return QStringLiteral("\\b(?:(?:every!?)\\s+(?:other\\s+(?:day|week|month|year)|\\d{1,3}\\s+(?:days?|weeks?|months?|years?)"
                          "|day|week|month|year|weekday|workday|weekend|\\d{1,2}(?:st|nd|rd|th)|%1)"
                          "|daily|weekly|monthly|yearly|annually)\\b")
        .arg(list);
}

Recurrence Recurrence::parse(const QString &textIn)
{
    Recurrence r;
    QString t = textIn.trimmed().toLower();
    t.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    if (t.isEmpty()) {
        return r;
    }
    if (t == QLatin1String("daily")) {
        t = QStringLiteral("every day");
    } else if (t == QLatin1String("weekly")) {
        t = QStringLiteral("every week");
    } else if (t == QLatin1String("monthly")) {
        t = QStringLiteral("every month");
    } else if (t == QLatin1String("yearly") || t == QLatin1String("annually")) {
        t = QStringLiteral("every year");
    }
    if (t.startsWith(QLatin1String("every! "))) {
        r.fromCompletion = true;
        t = t.mid(7);
    } else if (t.startsWith(QLatin1String("every "))) {
        t = t.mid(6);
    } else {
        return r;
    }
    t = t.trimmed();

    auto unitOf = [](const QString &w, Unit *u) {
        if (w.startsWith(QLatin1String("day"))) {
            *u = Unit::Day;
        } else if (w.startsWith(QLatin1String("week"))) {
            *u = Unit::Week;
        } else if (w.startsWith(QLatin1String("month"))) {
            *u = Unit::Month;
        } else if (w.startsWith(QLatin1String("year"))) {
            *u = Unit::Year;
        } else {
            return false;
        }
        return true;
    };

    static const QRegularExpression nUnits(QStringLiteral("^(\\d{1,3}) (days?|weeks?|months?|years?)$"));
    static const QRegularExpression other(QStringLiteral("^other (day|week|month|year)$"));
    static const QRegularExpression single(QStringLiteral("^(day|week|month|year)$"));
    static const QRegularExpression nth(QStringLiteral("^(\\d{1,2})(st|nd|rd|th)$"));
    if (auto m = nUnits.match(t); m.hasMatch()) {
        r.interval = m.captured(1).toInt();
        r.valid = r.interval > 0 && unitOf(m.captured(2), &r.unit);
        return r;
    }
    if (auto m = other.match(t); m.hasMatch()) {
        r.interval = 2;
        r.valid = unitOf(m.captured(1), &r.unit);
        return r;
    }
    if (auto m = single.match(t); m.hasMatch()) {
        r.valid = unitOf(m.captured(1), &r.unit);
        return r;
    }
    if (auto m = nth.match(t); m.hasMatch()) {
        r.unit = Unit::Month;
        r.monthDay = m.captured(1).toInt();
        r.valid = r.monthDay >= 1 && r.monthDay <= 31;
        return r;
    }
    if (t == QLatin1String("weekday") || t == QLatin1String("workday")) {
        r.unit = Unit::Weekdays;
        r.weekdays = {1, 2, 3, 4, 5};
        r.valid = true;
        return r;
    }
    if (t == QLatin1String("weekend")) {
        r.unit = Unit::Weekdays;
        r.weekdays = {6, 7};
        r.valid = true;
        return r;
    }
    // A list of weekday names.
    static const QRegularExpression word(weekdayPattern());
    QString rest = t;
    rest.replace(QRegularExpression(QStringLiteral("(,|\\band\\b|&)")), QStringLiteral(" "));
    const QStringList parts = rest.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        const auto m = word.match(p);
        if (!m.hasMatch() || m.capturedLength() != p.size()) {
            return Recurrence{};
        }
        const int d = weekdayFromName(p);
        if (!r.weekdays.contains(d)) {
            r.weekdays << d;
        }
    }
    std::sort(r.weekdays.begin(), r.weekdays.end());
    r.unit = Unit::Weekdays;
    r.valid = !r.weekdays.isEmpty();
    return r;
}

namespace {
QDate dayInMonth(int year, int month, int day)
{
    // The 31st in a 30-day month is the last day of that month.
    const QDate first(year, month, 1);
    return QDate(year, month, std::min(day, first.daysInMonth()));
}
} // namespace

int Recurrence::anchorFor(const QDate &due, int anchorDay)
{
    if (!due.isValid()) {
        return 0;
    }
    if (anchorDay > due.day() && anchorDay <= 31 && due.day() == due.daysInMonth()) {
        return anchorDay;
    }
    return due.day();
}

QDate Recurrence::next(const QDate &base, int anchorDay) const
{
    if (!valid || !base.isValid()) {
        return {};
    }
    switch (unit) {
    case Unit::Day:
        return base.addDays(interval);
    case Unit::Week:
        return base.addDays(7LL * interval);
    case Unit::Month: {
        if (monthDay > 0) {
            QDate d = dayInMonth(base.year(), base.month(), monthDay);
            if (d <= base) {
                const QDate n = base.addMonths(1);
                d = dayInMonth(n.year(), n.month(), monthDay);
            }
            return d;
        }
        const QDate d = base.addMonths(interval);
        return dayInMonth(d.year(), d.month(), anchorFor(base, anchorDay));
    }
    case Unit::Year: {
        const QDate d = base.addYears(interval);
        return dayInMonth(d.year(), d.month(), anchorFor(base, anchorDay));
    }
    case Unit::Weekdays:
        for (int i = 1; i <= 7; ++i) {
            const QDate d = base.addDays(i);
            if (weekdays.contains(d.dayOfWeek())) {
                return d;
            }
        }
        return {};
    }
    return {};
}

QDate Recurrence::first(const QDate &today) const
{
    if (!valid) {
        return {};
    }
    if (unit == Unit::Weekdays && !weekdays.contains(today.dayOfWeek())) {
        return next(today);
    }
    if (unit == Unit::Month && monthDay > 0) {
        return next(today.addDays(-1));
    }
    return today;
}

QDate Recurrence::afterCompletion(const QDate &due, const QDate &today, int anchorDay) const
{
    if (!valid) {
        return {};
    }
    const bool fromToday = fromCompletion || !due.isValid();
    // Every step keeps the same day of the month, even through a short month.
    const int anchor = fromToday ? 0 : anchorFor(due, anchorDay);
    QDate d = next(fromToday ? today : due, anchor);
    // Skip occurrences that are already in the past (an overdue daily task
    // moves to tomorrow, not to the day after it was due).
    for (int guard = 0; d.isValid() && d <= today && guard < 10000; ++guard) {
        d = next(d, anchor);
    }
    return d;
}

} // namespace ztodo
