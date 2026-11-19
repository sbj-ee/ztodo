// Fuzz the device-log reader: the bytes of devices/*.jsonl come from other
// machines through Box, so ztodo must survive anything in them.
//
// The input is used as this device's own log. The harness reads it through a
// real Store, uses every read API, writes one more change (exercising the
// hybrid clock after whatever ts the input claimed), compacts the log and
// checks the compaction promise: a fresh reader sees exactly the same state.
#include "FuzzMain.hpp"
#include "Store.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

using namespace ztodo;

namespace {

QString stateOf(const Store &s)
{
    QString out;
    for (const Task &t : s.tasks()) {
        out += QStringLiteral("T %1|%2|%3|%4|%5|%6|%7|%8|%9|")
                   .arg(t.id, t.title, t.notes, t.project, t.parent, t.labels.join(','))
                   .arg(t.priority)
                   .arg(t.dueString())
                   .arg(t.order)
            + t.recur + (t.done ? "|done\n" : "|\n");
        for (const Task &c : s.children(t.id)) {
            out += QStringLiteral("  child %1\n").arg(c.id);
        }
    }
    for (const Project &p : s.projects()) {
        out += QStringLiteral("P %1|%2|%3|%4\n").arg(p.id, p.name, p.color).arg(p.order);
    }
    for (const Label &l : s.labels()) {
        out += QStringLiteral("L %1|%2|%3\n").arg(l.id, l.name, l.color);
    }
    return out;
}

QTemporaryDir &workDir()
{
    static QTemporaryDir dir;
    return dir;
}

} // namespace

ZTODO_FUZZ_INIT
{
    static int argc = 1;
    static char name[] = "fuzz_oplog";
    static char *argv[] = {name, nullptr};
    static QCoreApplication app(argc, argv);
    return workDir().isValid() ? 0 : 1;
}

ZTODO_FUZZ_TARGET(const uint8_t *data, size_t size)
{
    const QString root = workDir().path() + QStringLiteral("/data");
    QDir(root).removeRecursively();
    QDir().mkpath(root + QStringLiteral("/devices"));
    // Only complete lines: Store skips a torn last line until it is finished,
    // while compaction (like put(), which terminates it first) counts it.
    QByteArray bytes(reinterpret_cast<const char *>(data), qsizetype(size));
    bytes.truncate(bytes.lastIndexOf('\n') + 1);
    {
        QFile f(root + QStringLiteral("/devices/fz.jsonl"));
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
            std::abort();
        }
    }

    // Compaction rewrites only this device's own ops, so the promise holds
    // when every op in its log is its own (the format guarantees that).
    bool allOwn = true;
    for (const QByteArray &line : bytes.split('\n')) {
        Op op;
        if (Op::fromJsonLine(line.trimmed(), &op) && op.device != QLatin1String("fz")) {
            allOwn = false;
        }
    }

    Store s;
    if (!s.open(root, QStringLiteral("fz"))) {
        return 0;
    }
    (void)stateOf(s);
    for (const Task &t : s.tasks()) {
        Task one;
        s.task(t.id, &one);
        (void)s.projectName(t.project);
        for (const QString &l : t.labels) {
            (void)s.labelName(l);
        }
    }
    (void)s.findProject(QStringLiteral("Work"));
    (void)s.findLabel(QStringLiteral("home"));
    (void)s.nextOrder(QStringLiteral("task"));

    // One more write after whatever the log said (clock near its limits,
    // existing ids, ...).
    const QString id = s.addTask(Task{});
    if (!id.isEmpty()) {
        s.complete(id);
    }

    s.reload();
    const QString before = stateOf(s);
    s.compactOwnLog();
    Store fresh;
    if (!fresh.open(root, QStringLiteral("other"))) {
        std::abort();
    }
    if (allOwn && stateOf(fresh) != before) {
        std::fprintf(stderr, "compaction changed the merged state\n--- before\n%s--- after\n%s",
                     qPrintable(before), qPrintable(stateOf(fresh)));
        std::abort();
    }
    return 0;
}
