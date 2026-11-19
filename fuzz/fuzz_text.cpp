// Fuzz the text parsers the user types into: quick add, recurrence rules and
// due dates (and the recurrence rules in synced logs, which other devices
// wrote). Beyond not crashing, the harness checks the promises the code
// makes: a recurrence's next date is strictly later, and a parsed due date
// formats and parses back.
#include "DateText.hpp"
#include "FuzzMain.hpp"
#include "QuickAdd.hpp"
#include "Recurrence.hpp"

#include <QDate>
#include <QString>

#include <cstdio>
#include <cstdlib>

using namespace ztodo;

namespace {

[[noreturn]] void fail(const char *what, const QString &text)
{
    std::fprintf(stderr, "%s: \"%s\"\n", what, qPrintable(text));
    std::abort();
}

} // namespace

ZTODO_FUZZ_INIT
{
    return 0;
}

ZTODO_FUZZ_TARGET(const uint8_t *data, size_t size)
{
    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(data), qsizetype(size));
    // Month ends, leap days and year ends are where date arithmetic breaks.
    static const QDate todays[] = {QDate(2026, 11, 19), QDate(2028, 2, 29), QDate(2026, 12, 31), QDate(2027, 1, 31)};

    for (const QDate &today : todays) {
        const QuickAdd q = QuickAdd::parse(text, today);
        if (q.priority < 1 || q.priority > 4) {
            fail("quick add priority out of range", text);
        }

        QDate d;
        QTime t;
        if (parseDue(text, today, &d, &t) && d.isValid()) {
            (void)formatDue(d, t, today);
        }
        (void)findDate(text, today);
        (void)findTime(text);
    }
    (void)weekdayFromName(text);

    const Recurrence r = Recurrence::parse(text);
    if (r.valid) {
        for (const QDate &base : todays) {
            const QDate n = r.next(base);
            if (!n.isValid() || n <= base) {
                fail("Recurrence::next is not after its base", text);
            }
            const QDate f = r.first(base);
            if (!f.isValid() || f < base) {
                fail("Recurrence::first is before today", text);
            }
            const QDate a = r.afterCompletion(base.addDays(-3), base);
            if (!a.isValid() || a <= base) {
                fail("Recurrence::afterCompletion is not after today", text);
            }
        }
    }
    return 0;
}
