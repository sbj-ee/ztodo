#include "DateText.hpp"

#include <QLocale>
#include <QRegularExpression>

#include <functional>

namespace ztodo {

namespace {

const QRegularExpression::PatternOptions kCi = QRegularExpression::CaseInsensitiveOption;

int monthFromName(const QString &w)
{
    static const char *names[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
    const QString l = w.left(3).toLower();
    for (int i = 0; i < 12; ++i) {
        if (l == QLatin1String(names[i])) {
            return i + 1;
        }
    }
    return 0;
}

const QString kMonth = QStringLiteral(
    "(january|february|march|april|may|june|july|august|september|october|november|december|"
    "jan|feb|mar|apr|jun|jul|aug|sept|sep|oct|nov|dec)");

// A month/day with no year: this year, or next year once it has passed.
QDate upcoming(int month, int day, const QDate &today)
{
    QDate d(today.year(), month, day);
    if (d.isValid() && d < today) {
        d = QDate(today.year() + 1, month, day);
    }
    return d;
}

int fullYear(const QString &y)
{
    const int n = y.toInt();
    return y.size() <= 2 ? 2000 + n : n;
}

struct Candidate {
    QRegularExpression re;
    std::function<QDate(const QRegularExpressionMatch &, const QDate &)> make;
};

const QList<Candidate> &candidates()
{
    static const QList<Candidate> list = [] {
        QList<Candidate> c;
        const QString wd = weekdayPattern();
        c.append({QRegularExpression(QStringLiteral("\\b(\\d{4})-(\\d{1,2})-(\\d{1,2})\\b")),
                  [](const QRegularExpressionMatch &m, const QDate &) {
                      return QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
                  }});
        c.append({QRegularExpression(QStringLiteral("\\b(\\d{1,2})/(\\d{1,2})(?:/(\\d{4}|\\d{2}))?\\b")),
                  [](const QRegularExpressionMatch &m, const QDate &today) {
                      const int mo = m.captured(1).toInt();
                      const int d = m.captured(2).toInt();
                      if (!m.captured(3).isEmpty()) {
                          return QDate(fullYear(m.captured(3)), mo, d);
                      }
                      return upcoming(mo, d, today);
                  }});
        c.append({QRegularExpression(QStringLiteral("\\b(?:today|tod)\\b"), kCi),
                  [](const QRegularExpressionMatch &, const QDate &t) { return t; }});
        c.append({QRegularExpression(QStringLiteral("\\b(?:tomorrow|tmrw|tmr)\\b"), kCi),
                  [](const QRegularExpressionMatch &, const QDate &t) { return t.addDays(1); }});
        c.append({QRegularExpression(QStringLiteral("\\byesterday\\b"), kCi),
                  [](const QRegularExpressionMatch &, const QDate &t) { return t.addDays(-1); }});
        c.append({QRegularExpression(QStringLiteral("\\bnext week\\b"), kCi),
                  [](const QRegularExpressionMatch &, const QDate &t) { return t.addDays(8 - t.dayOfWeek()); }});
        c.append({QRegularExpression(QStringLiteral("\\bnext month\\b"), kCi),
                  [](const QRegularExpressionMatch &, const QDate &t) {
                      const QDate n = t.addMonths(1);
                      return QDate(n.year(), n.month(), 1);
                  }});
        c.append({QRegularExpression(QStringLiteral("\\b(?:eom|end of (?:the )?month)\\b"), kCi),
                  [](const QRegularExpressionMatch &, const QDate &t) {
                      return QDate(t.year(), t.month(), t.daysInMonth());
                  }});
        c.append({QRegularExpression(QStringLiteral("\\bin (\\d{1,3}) (days?|weeks?|months?|years?)\\b"), kCi),
                  [](const QRegularExpressionMatch &m, const QDate &t) {
                      const int n = m.captured(1).toInt();
                      const QChar u = m.captured(2).at(0).toLower();
                      if (u == QLatin1Char('d')) {
                          return t.addDays(n);
                      }
                      if (u == QLatin1Char('w')) {
                          return t.addDays(7 * n);
                      }
                      if (u == QLatin1Char('m')) {
                          return t.addMonths(n);
                      }
                      return t.addYears(n);
                  }});
        // "nov 20", "november 20th", "nov 20 2027", "nov 20, 2027"
        c.append({QRegularExpression(QStringLiteral("\\b%1\\.? (\\d{1,2})(?:st|nd|rd|th)?(?:,? (\\d{4}))?\\b").arg(kMonth), kCi),
                  [](const QRegularExpressionMatch &m, const QDate &t) {
                      const int mo = monthFromName(m.captured(1));
                      const int d = m.captured(2).toInt();
                      if (!m.captured(3).isEmpty()) {
                          return QDate(m.captured(3).toInt(), mo, d);
                      }
                      return upcoming(mo, d, t);
                  }});
        // "20 nov", "20th november 2027"
        c.append({QRegularExpression(QStringLiteral("\\b(\\d{1,2})(?:st|nd|rd|th)? %1\\b\\.?(?: (\\d{4}))?").arg(kMonth), kCi),
                  [](const QRegularExpressionMatch &m, const QDate &t) {
                      const int mo = monthFromName(m.captured(2));
                      const int d = m.captured(1).toInt();
                      if (!m.captured(3).isEmpty()) {
                          return QDate(m.captured(3).toInt(), mo, d);
                      }
                      return upcoming(mo, d, t);
                  }});
        // "fri" / "this fri" / "on fri": that day this week, or today if it is
        // Friday; "next fri": the Friday after today (never today).
        c.append({QRegularExpression(QStringLiteral("\\b(?:(next|this|on) )?%1\\b").arg(wd), kCi),
                  [](const QRegularExpressionMatch &m, const QDate &t) {
                      const int want = weekdayFromName(m.captured(2));
                      int delta = (want - t.dayOfWeek() + 7) % 7;
                      if (delta == 0 && m.captured(1).compare(QLatin1String("next"), Qt::CaseInsensitive) == 0) {
                          delta = 7;
                      }
                      return t.addDays(delta);
                  }});
        return c;
    }();
    return list;
}

} // namespace

QString weekdayPattern()
{
    return QStringLiteral("(monday|tuesday|wednesday|thursday|friday|saturday|sunday|"
                          "mon|tues|tue|weds|wed|thurs|thur|thu|fri|sat|sun)");
}

int weekdayFromName(const QString &word)
{
    static const char *names[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
    const QString l = word.trimmed().left(3).toLower();
    for (int i = 0; i < 7; ++i) {
        if (l == QLatin1String(names[i])) {
            return i + 1;
        }
    }
    return 0;
}

TextMatch findDate(const QString &text, const QDate &today)
{
    TextMatch best;
    for (const Candidate &c : candidates()) {
        auto it = c.re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            const QDate d = c.make(m, today);
            if (!d.isValid()) {
                continue;
            }
            // Earliest match wins; at the same start, the longest.
            if (!best.found() || m.capturedStart() < best.start
                || (m.capturedStart() == best.start && m.capturedLength() > best.length)) {
                best.start = m.capturedStart();
                best.length = m.capturedLength();
                best.date = d;
            }
            break;
        }
    }
    return best;
}

TextMatch findTime(const QString &text)
{
    static const QRegularExpression ampm(QStringLiteral("\\b(?:at )?(\\d{1,2})(?::(\\d{2}))? ?(am|pm|a|p)\\b"), kCi);
    static const QRegularExpression h24(QStringLiteral("\\b(?:at )?([01]?\\d|2[0-3]):([0-5]\\d)\\b"), kCi);
    static const QRegularExpression atHour(QStringLiteral("\\bat (\\d{1,2})\\b"), kCi);
    static const QRegularExpression named(QStringLiteral("\\b(?:at )?(noon|midday|midnight)\\b"), kCi);
    TextMatch r;
    auto consider = [&r](const QRegularExpressionMatch &m, const QTime &t) {
        if (!m.hasMatch() || !t.isValid()) {
            return;
        }
        if (!r.found() || m.capturedStart() < r.start) {
            r.start = m.capturedStart();
            r.length = m.capturedLength();
            r.time = t;
        }
    };
    {
        const auto m = ampm.match(text);
        if (m.hasMatch()) {
            int h = m.captured(1).toInt();
            const int mi = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
            const bool pm = m.captured(3).startsWith(QLatin1Char('p'), Qt::CaseInsensitive);
            if (h >= 1 && h <= 12) {
                h = (h % 12) + (pm ? 12 : 0);
                consider(m, QTime(h, mi));
            }
        }
    }
    consider(h24.match(text), [&] {
        const auto m = h24.match(text);
        return m.hasMatch() ? QTime(m.captured(1).toInt(), m.captured(2).toInt()) : QTime();
    }());
    {
        const auto m = atHour.match(text);
        if (m.hasMatch()) {
            const int h = m.captured(1).toInt();
            // "at 9" means 9 AM .. "at 6" means 6 PM: hours 1-6 are read as afternoon.
            if (h >= 0 && h <= 23) {
                consider(m, QTime(h >= 1 && h <= 6 ? h + 12 : h, 0));
            }
        }
    }
    {
        const auto m = named.match(text);
        if (m.hasMatch()) {
            consider(m, m.captured(1).compare(QLatin1String("midnight"), Qt::CaseInsensitive) == 0 ? QTime(0, 0)
                                                                                                  : QTime(12, 0));
        }
    }
    return r;
}

bool parseDue(const QString &text, const QDate &today, QDate *date, QTime *time)
{
    QString rest = text;
    QDate d;
    QTime t;
    const TextMatch dm = findDate(rest, today);
    if (dm.found()) {
        d = dm.date;
        rest.remove(dm.start, dm.length);
    }
    const TextMatch tm = findTime(rest);
    if (tm.found()) {
        t = tm.time;
        rest.remove(tm.start, tm.length);
        if (!d.isValid()) {
            d = today;
        }
    }
    if (!rest.trimmed().isEmpty()) {
        return false;
    }
    *date = d;
    *time = t;
    return true;
}

QString formatDue(const QDate &date, const QTime &time, const QDate &today)
{
    if (!date.isValid()) {
        return {};
    }
    const QLocale c(QLocale::English);
    QString s;
    const qint64 delta = today.daysTo(date);
    if (delta == 0) {
        s = QStringLiteral("Today");
    } else if (delta == 1) {
        s = QStringLiteral("Tomorrow");
    } else if (delta == -1) {
        s = QStringLiteral("Yesterday");
    } else if (delta > 1 && delta < 7) {
        s = c.dayName(date.dayOfWeek());
    } else if (date.year() == today.year()) {
        s = c.toString(date, QStringLiteral("MMM d"));
    } else {
        s = c.toString(date, QStringLiteral("MMM d yyyy"));
    }
    if (time.isValid()) {
        s += QLatin1Char(' ') + c.toString(time, QStringLiteral("h:mm AP"));
    }
    return s;
}

} // namespace ztodo
