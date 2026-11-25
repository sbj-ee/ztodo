// Randomized checks of the merge guarantees in docs/STORAGE.md:
//
//  - the merged state is the same whatever order changes arrive in, and
//    however they are split across files (conflict copies, duplicates);
//  - reading a change twice changes nothing;
//  - compaction never changes the merged state, including for a reader that
//    keeps reading a log incrementally while it is compacted.
//
// The oracle is the rule itself, written as plainly as possible: for every
// (kind, id, field), the value from the change with the highest (ts, device)
// among every change ever written. Every Store must agree with it.
//
// Each case runs many seeds. A failure prints its seed; ZTODO_MERGE_SEED=<n>
// runs only that seed, and ZTODO_MERGE_ROUNDS=<n> runs more of them.
#include "Store.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

#include <map>
#include <memory>
#include <tuple>

using namespace ztodo;

namespace {

const QStringList kKinds = {"task", "project", "label"};
const QStringList kDevices = {"dev-a", "dev-b", "dev-c"};

QStringList fieldsFor(const QString &kind)
{
    if (kind == "task") {
        return {"title", "notes", "priority", "labels", "done", "due", "project", "parent", "order", "deleted"};
    }
    if (kind == "project") {
        return {"name", "color", "order", "deleted"};
    }
    return {"name", "color", "deleted"};
}

QStringList allFields()
{
    QStringList all;
    for (const QString &k : kKinds) {
        for (const QString &f : fieldsFor(k)) {
            if (!all.contains(f)) {
                all << f;
            }
        }
    }
    return all;
}

// Mostly well-typed values from small pools, so that ties (same order, names
// equal but for case) and overwrites are common; sometimes a wrong type, as a
// buggy or future writer might produce.
QJsonValue randomValue(QRandomGenerator &r, const QString &field)
{
    if (r.bounded(10) == 0) {
        switch (r.bounded(5)) {
        case 0: return QJsonValue();
        case 1: return 42;
        case 2: return QStringLiteral("x");
        case 3: return QJsonArray{1, "two"};
        default: return QJsonObject{{"nested", true}};
        }
    }
    static const QStringList names = {"Work", "work", "Home", "Ünïcødé", "", "with \"quotes\"\n"};
    if (field == "title" || field == "notes" || field == "name") {
        return names.at(int(r.bounded(names.size())));
    }
    if (field == "priority") {
        return int(r.bounded(6)); // 0 and 5 are out of range on purpose
    }
    if (field == "labels") {
        QJsonArray a;
        for (int i = 0, n = int(r.bounded(3)); i < n; ++i) {
            a << QStringLiteral("e%1").arg(r.bounded(4));
        }
        return a;
    }
    if (field == "done" || field == "deleted") {
        return r.bounded(2) == 0;
    }
    if (field == "due") {
        static const QStringList dues = {"", "2026-11-20", "2026-11-20T17:00", "2026-02-30", "garbage"};
        return dues.at(int(r.bounded(dues.size())));
    }
    if (field == "order") {
        return double(r.bounded(4)) / 2;
    }
    if (field == "color") {
        return r.bounded(2) == 0 ? "#4fc33a" : "#8a8f98";
    }
    return QStringLiteral("e%1").arg(r.bounded(4)); // project, parent
}

Op randomOp(QRandomGenerator &r, qint64 ts, const QString &device)
{
    Op op;
    op.ts = ts;
    op.device = device;
    op.kind = kKinds.at(int(r.bounded(kKinds.size())));
    op.id = QStringLiteral("e%1").arg(r.bounded(4));
    const QStringList fields = fieldsFor(op.kind);
    for (int i = 0, n = 1 + int(r.bounded(3)); i < n; ++i) {
        const QString f = fields.at(int(r.bounded(fields.size())));
        op.fields.insert(f, randomValue(r, f));
    }
    return op;
}

// The merge rule, written independently of Store.
class Oracle {
public:
    void add(const Op &op)
    {
        for (auto it = op.fields.begin(); it != op.fields.end(); ++it) {
            Winner &w = m_state[{op.kind, op.id, it.key()}];
            if (!w.set || std::tie(op.ts, op.device) > std::tie(w.ts, w.dev)) {
                w = {true, op.ts, op.device, it.value()};
            }
        }
    }
    QJsonValue value(const QString &kind, const QString &id, const QString &field) const
    {
        const auto it = m_state.find({kind, id, field});
        return it == m_state.end() ? QJsonValue() : it->second.v;
    }

private:
    struct Winner {
        bool set = false;
        qint64 ts = 0;
        QString dev;
        QJsonValue v;
    };
    std::map<std::tuple<QString, QString, QString>, Winner> m_state;
};

// Everything a Store exposes about the merged state: every field of every
// entity in the pool (deleted ones too), plus the derived lists the UI shows.
QString fingerprint(const Store &s)
{
    QString out;
    for (const QString &kind : kKinds) {
        for (int i = 0; i < 4; ++i) {
            const QString id = QStringLiteral("e%1").arg(i);
            out += kind + ':' + id + ' '
                + QString::fromUtf8(QJsonDocument(s.snapshot(kind, id, allFields())).toJson(QJsonDocument::Compact))
                + '\n';
        }
    }
    for (const Task &t : s.tasks()) {
        out += QStringLiteral("T %1|%2|%3|%4|%5|%6|%7|%8|%9\n")
                   .arg(t.id, t.title, t.notes, t.project, t.parent, t.labels.join(','))
                   .arg(t.priority)
                   .arg(t.dueString())
                   .arg(t.order);
    }
    for (const Project &p : s.projects()) {
        out += QStringLiteral("P %1|%2|%3|%4\n").arg(p.id, p.name, p.color).arg(p.order);
    }
    for (const Label &l : s.labels()) {
        out += QStringLiteral("L %1|%2|%3\n").arg(l.id, l.name, l.color);
    }
    return out;
}

// What the oracle says the store's per-field state should be.
QString expectedFields(const Oracle &o)
{
    QString out;
    for (const QString &kind : kKinds) {
        for (int i = 0; i < 4; ++i) {
            const QString id = QStringLiteral("e%1").arg(i);
            QJsonObject obj;
            for (const QString &f : allFields()) {
                obj.insert(f, o.value(kind, id, f));
            }
            out += kind + ':' + id + ' ' + QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)) + '\n';
        }
    }
    return out;
}

QString fieldsPart(const QString &fp)
{
    QStringList keep;
    for (const QString &l : fp.split('\n')) {
        if (l.startsWith("task:") || l.startsWith("project:") || l.startsWith("label:")) {
            keep << l;
        }
    }
    return keep.join('\n') + '\n';
}

// Equal, or print the lines that differ (a failure should say why).
bool same(const QString &got, const QString &want)
{
    if (got == want) {
        return true;
    }
    const QStringList g = got.split('\n');
    const QStringList w = want.split('\n');
    for (qsizetype i = 0; i < std::max(g.size(), w.size()); ++i) {
        const QString a = g.value(i);
        const QString b = w.value(i);
        if (a != b) {
            qWarning("line %d\n  got:  %s\n  want: %s", int(i), qPrintable(a), qPrintable(b));
        }
    }
    return false;
}

bool writeFile(const QString &path, const QByteArray &bytes, bool append = false)
{
    QFile f(path);
    if (!f.open(append ? (QIODevice::WriteOnly | QIODevice::Append) : QIODevice::WriteOnly)) {
        return false;
    }
    return f.write(bytes) == bytes.size();
}

QList<quint32> seeds(int defaultRounds)
{
    bool ok = false;
    const quint32 one = qEnvironmentVariable("ZTODO_MERGE_SEED").toUInt(&ok);
    if (ok) {
        return {one};
    }
    const int n = qEnvironmentVariableIsSet("ZTODO_MERGE_ROUNDS") ? qEnvironmentVariableIntValue("ZTODO_MERGE_ROUNDS")
                                                                 : defaultRounds;
    QList<quint32> s;
    for (int i = 1; i <= n; ++i) {
        s << quint32(i);
    }
    return s;
}

} // namespace

class TstMerge : public QObject {
    Q_OBJECT

private slots:
    // The same set of changes, delivered in different ways, always merges to
    // the oracle's state: shuffled lines, lines spread over conflict copies,
    // duplicated lines, and files arriving piecemeal (with torn last lines)
    // to a reader that reloads in between.
    void arrivalOrderAndDuplicatesDontMatter()
    {
        for (const quint32 seed : seeds(150)) {
            QRandomGenerator r(seed);
            Oracle oracle;
            // Each device's ts is strictly increasing (as Store::tick() makes
            // it); devices overlap, so equal ts across devices are common.
            QList<Op> ops;
            for (const QString &dev : kDevices) {
                qint64 ts = 0;
                for (int i = 0, n = 5 + int(r.bounded(25)); i < n; ++i) {
                    ts += 1 + r.bounded(3);
                    ops << randomOp(r, ts, dev);
                }
            }
            for (const Op &op : std::as_const(ops)) {
                oracle.add(op);
            }
            const QString want = expectedFields(oracle);

            // 1. Each device's log, in order: the normal case.
            QTemporaryDir canon;
            QVERIFY(QDir(canon.path()).mkpath("devices"));
            for (const Op &op : std::as_const(ops)) {
                QVERIFY(writeFile(canon.path() + "/devices/" + op.device + ".jsonl", op.toJsonLine(), true));
            }
            Store a;
            QVERIFY(a.open(canon.path(), "reader"));
            QVERIFY2(same(fieldsPart(fingerprint(a)), want), qPrintable(QStringLiteral("seed %1: in-order logs").arg(seed)));
            const QString full = fingerprint(a);

            // 2. Lines shuffled, spread over files with conflict-copy names,
            //    some written twice.
            static const QStringList names = {"dev-a.jsonl", "dev-b.jsonl", "dev-a.jsonl.conflict1",
                                              "dev-b (conflicted copy 2026-11-19).jsonl", "x.jsonl.1"};
            QList<QByteArray> lines;
            for (const Op &op : std::as_const(ops)) {
                lines << op.toJsonLine();
                if (r.bounded(5) == 0) {
                    lines << op.toJsonLine();
                }
            }
            std::shuffle(lines.begin(), lines.end(), r);
            QTemporaryDir mixed;
            QVERIFY(QDir(mixed.path()).mkpath("devices"));
            for (const QByteArray &l : std::as_const(lines)) {
                QVERIFY(writeFile(mixed.path() + "/devices/" + names.at(int(r.bounded(names.size()))), l, true));
            }
            Store b;
            QVERIFY(b.open(mixed.path(), "reader"));
            QVERIFY2(same(fingerprint(b), full), qPrintable(QStringLiteral("seed %1: shuffled, duplicated, split").arg(seed)));

            // 3. Piecemeal: a long-lived reader reloads while the files grow,
            //    sometimes mid-line. Then everything once more (idempotence).
            QTemporaryDir slow;
            QVERIFY(QDir(slow.path()).mkpath("devices"));
            Store c;
            QVERIFY(c.open(slow.path(), "reader"));
            QHash<QString, QByteArray> pending;
            for (const Op &op : std::as_const(ops)) {
                pending[op.device] += op.toJsonLine();
            }
            while (!pending.isEmpty()) {
                const QString dev = pending.keys().at(int(r.bounded(pending.size())));
                QByteArray &rest = pending[dev];
                const qsizetype n = std::min<qsizetype>(rest.size(), 1 + r.bounded(200));
                QVERIFY(writeFile(slow.path() + "/devices/" + dev + ".jsonl", rest.left(n), true));
                rest.remove(0, n);
                if (rest.isEmpty()) {
                    pending.remove(dev);
                }
                if (r.bounded(2) == 0) {
                    c.reload();
                }
            }
            c.reload();
            QVERIFY2(same(fingerprint(c), full), qPrintable(QStringLiteral("seed %1: piecemeal arrival").arg(seed)));
            for (const Op &op : std::as_const(ops)) {
                QVERIFY(writeFile(slow.path() + "/devices/again.jsonl", op.toJsonLine(), true));
            }
            c.reload();
            QVERIFY2(same(fingerprint(c), full), qPrintable(QStringLiteral("seed %1: everything read twice").arg(seed)));
        }
    }

    // Three devices write through the real Store API, with skewed clocks,
    // restarts and compactions at random moments, while an observer keeps
    // reading incrementally. At the end every device, the observer and a
    // fresh reader agree with the oracle built from every change written.
    void compactionNeverChangesTheMergedState()
    {
        for (const quint32 seed : seeds(60)) {
            QRandomGenerator r(seed);
            QTemporaryDir tmp;
            Oracle oracle;
            qint64 wall = 1'000'000;
            QHash<QString, qint64> skew; // some devices' clocks are behind
            std::map<QString, std::unique_ptr<Store>> dev;
            auto start = [&](const QString &d) {
                auto s = std::make_unique<Store>();
                const qint64 sk = skew.value(d);
                s->setClockForTests([&wall, sk] { return wall + sk; });
                QVERIFY(s->open(tmp.path(), d));
                dev[d] = std::move(s);
            };
            for (const QString &d : kDevices) {
                skew[d] = -qint64(r.bounded(50));
                start(d);
            }
            Store observer;
            QVERIFY(observer.open(tmp.path(), "observer"));

            for (int step = 0, steps = 60 + int(r.bounded(140)); step < steps; ++step) {
                wall += r.bounded(3); // often unchanged: forces clock ties
                const QString d = kDevices.at(int(r.bounded(kDevices.size())));
                Store &s = *dev[d];
                const quint32 action = r.bounded(20);
                if (action < 12) {
                    const Op shape = randomOp(r, 0, d);
                    QVERIFY(s.put(shape.kind, shape.id, shape.fields));
                    // The op exactly as written, ts included, for the oracle.
                    QFile f(s.ownLogPath());
                    QVERIFY(f.open(QIODevice::ReadOnly));
                    const QList<QByteArray> written = f.readAll().trimmed().split('\n');
                    Op op;
                    QVERIFY(Op::fromJsonLine(written.last(), &op));
                    // put() adds the anchor day to a task's due change.
                    QJsonObject expected = shape.fields;
                    if (shape.kind == "task" && expected.contains("due")) {
                        const QDate due = QDate::fromString(expected.value("due").toString().left(10), Qt::ISODate);
                        expected.insert("anchor", due.isValid() ? due.day() : 0);
                    }
                    QCOMPARE(op.fields, expected);
                    oracle.add(op);
                } else if (action < 16) {
                    s.reload();
                } else if (action < 18) {
                    s.compactOwnLog();
                } else if (action < 19) {
                    dev.erase(d); // restart
                    start(d);
                } else {
                    observer.reload();
                }
                if (r.bounded(4) == 0) {
                    observer.reload();
                }
            }

            const QString want = expectedFields(oracle);
            for (auto &[d, s] : dev) {
                s->reload();
                QVERIFY2(same(fieldsPart(fingerprint(*s)), want),
                         qPrintable(QStringLiteral("seed %1: %2 disagrees with the oracle").arg(seed).arg(d)));
            }
            observer.reload();
            const QString full = fingerprint(observer);
            QVERIFY2(same(fieldsPart(full), want), qPrintable(QStringLiteral("seed %1: observer").arg(seed)));
            Store fresh;
            QVERIFY(fresh.open(tmp.path(), "fresh"));
            QVERIFY2(same(fingerprint(fresh), full), qPrintable(QStringLiteral("seed %1: fresh reader").arg(seed)));
            for (auto &[d, s] : dev) {
                QVERIFY2(same(fingerprint(*s), full), qPrintable(QStringLiteral("seed %1: %2's lists").arg(seed).arg(d)));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TstMerge)
#include "tst_merge.moc"
