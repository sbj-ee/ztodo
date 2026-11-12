#include "QuickAdd.hpp"

#include "DateText.hpp"
#include "Recurrence.hpp"

#include <QRegularExpression>

namespace ztodo {

QuickAdd QuickAdd::parse(const QString &text, const QDate &today)
{
    QuickAdd q;
    QString rest = text;

    static const QRegularExpression recur(recurrencePattern(), QRegularExpression::CaseInsensitiveOption);
    if (const auto m = recur.match(rest); m.hasMatch()) {
        const QString phrase = m.captured(0).simplified();
        // A bare "daily" / "monthly" counts only at the end of the text (before
        // any #project, @label or p1 tokens): "Review monthly budget" is a title.
        static const QRegularExpression tail(QStringLiteral("^(?:\\s*(?:[#@]\\S+|[pP][1-4]\\b))*\\s*$"));
        const bool bare = !phrase.startsWith(QLatin1String("every"), Qt::CaseInsensitive);
        const bool atEnd = tail.match(rest.mid(m.capturedEnd())).hasMatch();
        if (Recurrence::parse(phrase).valid && (!bare || atEnd)) {
            q.recur = phrase.toLower();
            rest.remove(m.capturedStart(), m.capturedLength());
        }
    }

    const TextMatch dm = findDate(rest, today);
    if (dm.found()) {
        q.due = dm.date;
        rest.remove(dm.start, dm.length);
    }
    const TextMatch tm = findTime(rest);
    if (tm.found()) {
        q.time = tm.time;
        rest.remove(tm.start, tm.length);
    }
    if (!q.recur.isEmpty() && !q.due.isValid()) {
        q.due = Recurrence::parse(q.recur).first(today);
    }
    if (q.time.isValid() && !q.due.isValid()) {
        q.due = today;
    }

    static const QRegularExpression prio(QStringLiteral("(?:^|\\s)[pP]([1-4])(?=\\s|$)"));
    if (const auto m = prio.match(rest); m.hasMatch()) {
        q.priority = m.captured(1).toInt();
        rest.remove(m.capturedStart(), m.capturedLength());
    }
    static const QRegularExpression proj(QStringLiteral("(?:^|\\s)#([^\\s#@]+)"));
    if (const auto m = proj.match(rest); m.hasMatch()) {
        q.project = m.captured(1);
        q.project.replace(QLatin1Char('_'), QLatin1Char(' '));
        rest.remove(m.capturedStart(), m.capturedLength());
    }
    static const QRegularExpression label(QStringLiteral("(?:^|\\s)@([^\\s#@]+)"));
    for (auto m = label.match(rest); m.hasMatch(); m = label.match(rest)) {
        QString name = m.captured(1);
        name.replace(QLatin1Char('_'), QLatin1Char(' '));
        if (!q.labels.contains(name, Qt::CaseInsensitive)) {
            q.labels << name;
        }
        rest.remove(m.capturedStart(), m.capturedLength());
    }

    q.title = rest.simplified();
    // Dangling joiners left behind by a removed phrase ("Call mom on" ...).
    static const QRegularExpression trailing(QStringLiteral("\\s+(on|at|by|due)$"), QRegularExpression::CaseInsensitiveOption);
    q.title.remove(trailing);
    if (q.title.isEmpty()) {
        q.title = text.simplified();
    }
    return q;
}

} // namespace ztodo
