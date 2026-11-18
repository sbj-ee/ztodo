#include "Store.hpp"

#include "Recurrence.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace ztodo {

namespace {
const QString kTask = QStringLiteral("task");
const QString kProject = QStringLiteral("project");
const QString kLabel = QStringLiteral("label");
const QString kDeleted = QStringLiteral("deleted");

bool validKind(const QString &k)
{
    return k == kTask || k == kProject || k == kLabel;
}

// Device ids and entity ids: short, file-name safe.
bool validId(const QString &s)
{
    if (s.isEmpty() || s.size() > 64) {
        return false;
    }
    for (QChar c : s) {
        if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != QLatin1Char('-') && c != QLatin1Char('_')) {
            return false;
        }
    }
    return true;
}

QString normalizedName(const QString &s)
{
    QString n = s.toLower();
    n.remove(QLatin1Char(' '));
    n.remove(QLatin1Char('_'));
    return n;
}
} // namespace

QString newId()
{
    return QUuid::createUuid().toString(QUuid::Id128).left(20);
}

QString Task::dueString() const
{
    if (!dueDate.isValid()) {
        return {};
    }
    QString s = dueDate.toString(Qt::ISODate);
    if (dueTime.isValid()) {
        s += QLatin1Char('T') + dueTime.toString(QStringLiteral("HH:mm"));
    }
    return s;
}

QString parseDueInto(const QString &s, QDate *date, QTime *time)
{
    *date = QDate::fromString(s.left(10), Qt::ISODate);
    *time = s.size() > 11 ? QTime::fromString(s.mid(11, 5), QStringLiteral("HH:mm")) : QTime();
    return s;
}

QByteArray Op::toJsonLine() const
{
    QJsonObject o;
    o.insert(QStringLiteral("ts"), ts);
    o.insert(QStringLiteral("dev"), device);
    o.insert(QStringLiteral("k"), kind);
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("f"), fields);
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

bool Op::fromJsonLine(const QByteArray &line, Op *out)
{
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(line, &pe);
    if (pe.error != QJsonParseError::NoError || !d.isObject()) {
        return false;
    }
    const QJsonObject o = d.object();
    Op op;
    op.ts = o.value(QStringLiteral("ts")).toInteger(-1);
    op.device = o.value(QStringLiteral("dev")).toString();
    op.kind = o.value(QStringLiteral("k")).toString();
    op.id = o.value(QStringLiteral("id")).toString();
    op.fields = o.value(QStringLiteral("f")).toObject();
    if (op.ts < 0 || !validId(op.device) || !validKind(op.kind) || !validId(op.id)) {
        return false;
    }
    *out = op;
    return true;
}

Store::Store(QObject *parent)
    : QObject(parent)
    , m_now([] { return QDateTime::currentMSecsSinceEpoch(); })
{
}

Store::~Store() = default;

QString Store::ownLogPath() const
{
    return m_dir + QStringLiteral("/devices/") + m_device + QStringLiteral(".jsonl");
}

bool Store::open(const QString &dirIn, const QString &deviceId, QString *error)
{
    auto fail = [error](const QString &m) {
        if (error) {
            *error = m;
        }
        return false;
    };
    close();
    if (!validId(deviceId)) {
        return fail(QStringLiteral("Invalid device id \"%1\".").arg(deviceId));
    }
    const QString dir = QDir::cleanPath(QFileInfo(dirIn).absoluteFilePath());
    QDir d(dir);
    if (!d.mkpath(QStringLiteral("devices"))) {
        return fail(QStringLiteral("Couldn't create the data folder %1.").arg(dir));
    }
    const QString marker = d.filePath(QStringLiteral("ztodo.json"));
    if (!QFileInfo::exists(marker)) {
        QSaveFile f(marker);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("{\"format\":\"ztodo\",\"version\":1}\n");
            f.commit();
        }
    } else {
        QFile f(marker);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            if (o.value(QStringLiteral("format")).toString() == QLatin1String("ztodo")
                && o.value(QStringLiteral("version")).toInt() > 1) {
                return fail(QStringLiteral("%1 was written by a newer ztodo (data format %2). Update ztodo first.")
                                .arg(dir)
                                .arg(o.value(QStringLiteral("version")).toInt()));
            }
        }
    }
    m_dir = dir;
    m_device = deviceId;
    reload();

    m_watcher = new QFileSystemWatcher(this);
    m_watcher->addPath(m_dir + QStringLiteral("/devices"));
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &Store::scheduleReload);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &Store::scheduleReload);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(400);
    connect(m_debounce, &QTimer::timeout, this, [this] {
        if (reload()) {
            emit changed();
        }
    });
    m_poll = new QTimer(this);
    m_poll->setInterval(15000);
    connect(m_poll, &QTimer::timeout, this, [this] {
        if (reload()) {
            emit changed();
        }
    });
    m_poll->start();
    return true;
}

void Store::close()
{
    delete m_watcher;
    m_watcher = nullptr;
    delete m_poll;
    m_poll = nullptr;
    delete m_debounce;
    m_debounce = nullptr;
    m_dir.clear();
    m_entities.clear();
    m_maxOrder.clear();
    m_stamps.clear();
    ++m_revision;
}

void Store::setPollInterval(int ms)
{
    if (m_poll) {
        m_poll->setInterval(ms);
    }
}

void Store::scheduleReload()
{
    if (m_debounce) {
        m_debounce->start();
    }
}

int Store::logFileCount(const QString &dir)
{
    return int(QDir(dir + QStringLiteral("/devices")).entryList({QStringLiteral("*.jsonl*")}, QDir::Files).size());
}

bool Store::reload()
{
    if (m_dir.isEmpty()) {
        return false;
    }
    bool any = false;
    const QDir d(m_dir + QStringLiteral("/devices"));
    // "*.jsonl*" includes sync conflict copies (x.jsonl.conflict1).
    const QFileInfoList files = d.entryInfoList({QStringLiteral("*.jsonl*")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        const auto it = m_stamps.constFind(fi.filePath());
        if (it != m_stamps.cend() && it->size == fi.size() && it->mtime == fi.lastModified()) {
            continue;
        }
        // Only the appended part is read; applying an op twice changes
        // nothing, so a full re-read (file rewritten) is always safe.
        FileStamp st = it != m_stamps.cend() ? *it : FileStamp{};
        st.size = fi.size();
        st.mtime = fi.lastModified();
        if (readFile(fi.filePath(), &st)) {
            any = true;
        }
        m_stamps.insert(fi.filePath(), st);
        if (m_watcher && !m_watcher->files().contains(fi.filePath())) {
            m_watcher->addPath(fi.filePath());
        }
    }
    if (any) {
        ++m_revision;
    }
    return any;
}

namespace {
constexpr qint64 kTailBytes = 32;

QByteArray tailBefore(QFile &f, qint64 offset)
{
    const qint64 n = std::min(offset, kTailBytes);
    if (n <= 0 || !f.seek(offset - n)) {
        return {};
    }
    return f.read(n);
}
} // namespace

Store::FileStamp Store::stampFor(const QString &path, qint64 consumed)
{
    const QFileInfo fi(path);
    FileStamp st{fi.size(), fi.lastModified(), consumed, {}};
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        st.tail = tailBefore(f, consumed);
    }
    return st;
}

bool Store::readFile(const QString &path, FileStamp *stamp)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    // Resume after the last complete line we applied, if the bytes before it
    // are unchanged; otherwise start over.
    qint64 start = 0;
    if (stamp->offset > 0 && stamp->offset <= f.size() && tailBefore(f, stamp->offset) == stamp->tail) {
        start = stamp->offset;
    }
    if (!f.seek(start)) {
        return false;
    }
    const QByteArray all = f.readAll();
    bool changedAny = false;
    qsizetype pos = 0;
    while (pos < all.size()) {
        const qsizetype nl = all.indexOf('\n', pos);
        if (nl < 0) {
            break; // an incomplete last line (being written or synced): next time
        }
        const QByteArray line = all.mid(pos, nl - pos).trimmed();
        pos = nl + 1;
        Op op;
        if (!line.isEmpty() && Op::fromJsonLine(line, &op)) {
            changedAny |= applyOp(op);
        }
    }
    stamp->offset = start + pos;
    stamp->tail = tailBefore(f, stamp->offset);
    return changedAny;
}

bool Store::applyOp(const Op &op)
{
    m_clock = std::max(m_clock, op.ts);
    Entity &e = m_entities[key(op.kind, op.id)];
    bool changedAny = false;
    for (auto it = op.fields.begin(); it != op.fields.end(); ++it) {
        FieldVal &cur = e.f[it.key()];
        // Last writer wins: higher timestamp, then higher device id.
        if (op.ts > cur.ts || (op.ts == cur.ts && op.device > cur.dev)) {
            if (cur.v != it.value() || cur.ts == 0) {
                changedAny = true;
            }
            if (it.key() == QLatin1String("order")) {
                double &mx = m_maxOrder[op.kind]; // kept incrementally: addTask() needs it per call
                mx = std::max(mx, it.value().toDouble());
            }
            cur.v = it.value();
            cur.ts = op.ts;
            cur.dev = op.device;
        }
    }
    return changedAny;
}

qint64 Store::tick()
{
    m_clock = std::max(m_clock + 1, m_now());
    return m_clock;
}

bool Store::put(const QString &kind, const QString &id, const QJsonObject &fields)
{
    if (m_dir.isEmpty() || !validKind(kind) || !validId(id) || fields.isEmpty()) {
        return false;
    }
    Op op;
    op.ts = tick();
    op.device = m_device;
    op.kind = kind;
    op.id = id;
    op.fields = fields;
    QFile f(ownLogPath());
    if (!f.open(QIODevice::ReadWrite | QIODevice::Append)) {
        emit error(QStringLiteral("Couldn't write %1: %2").arg(f.fileName(), f.errorString()));
        return false;
    }
    // A torn last line (crash mid-write) would swallow this op; end it first.
    if (f.size() > 0) {
        f.seek(f.size() - 1);
        char last = '\n';
        f.getChar(&last);
        if (last != '\n') {
            f.write("\n");
        }
    }
    const QByteArray line = op.toJsonLine();
    if (f.write(line) != line.size() || !f.flush()) {
        emit error(QStringLiteral("Couldn't write %1: %2").arg(f.fileName(), f.errorString()));
        return false;
    }
    f.close();
    applyOp(op);
    m_stamps.insert(QFileInfo(f.fileName()).filePath(), stampFor(f.fileName(), QFileInfo(f.fileName()).size()));
    ++m_revision;
    if (m_batch > 0) {
        m_batchDirty = true;
    } else {
        emit changed();
    }
    return true;
}

QString Store::addTask(const Task &t)
{
    const QString id = t.id.isEmpty() ? newId() : t.id;
    QJsonObject f;
    f.insert(QStringLiteral("title"), t.title);
    if (!t.notes.isEmpty()) {
        f.insert(QStringLiteral("notes"), t.notes);
    }
    f.insert(QStringLiteral("project"), t.project);
    if (!t.parent.isEmpty()) {
        f.insert(QStringLiteral("parent"), t.parent);
    }
    f.insert(QStringLiteral("labels"), QJsonArray::fromStringList(t.labels));
    f.insert(QStringLiteral("priority"), t.priority);
    f.insert(QStringLiteral("due"), t.dueString());
    f.insert(QStringLiteral("recur"), t.recur);
    f.insert(QStringLiteral("done"), false);
    f.insert(QStringLiteral("created"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    f.insert(QStringLiteral("order"), t.order != 0 ? t.order : nextOrder(kTask));
    return put(kTask, id, f) ? id : QString();
}

QString Store::addProject(const QString &name, const QString &color)
{
    const QString id = newId();
    QJsonObject f;
    f.insert(QStringLiteral("name"), name.trimmed());
    f.insert(QStringLiteral("color"), color.isEmpty() ? QStringLiteral("#4fc33a") : color);
    f.insert(QStringLiteral("order"), nextOrder(kProject));
    return put(kProject, id, f) ? id : QString();
}

QString Store::addLabel(const QString &name, const QString &color)
{
    const QString id = newId();
    QJsonObject f;
    f.insert(QStringLiteral("name"), name.trimmed());
    f.insert(QStringLiteral("color"), color.isEmpty() ? QStringLiteral("#8a8f98") : color);
    return put(kLabel, id, f) ? id : QString();
}

QJsonObject Store::snapshot(const QString &kind, const QString &id, const QStringList &fields) const
{
    QJsonObject o;
    const Entity *e = entity(kind, id);
    for (const QString &name : fields) {
        o.insert(name, e && e->f.contains(name) ? e->f.value(name).v : QJsonValue());
    }
    return o;
}

QJsonObject Store::complete(const QString &taskId, const QDate &today)
{
    Task t;
    if (!task(taskId, &t)) {
        return {};
    }
    const Recurrence r = Recurrence::parse(t.recur);
    if (r.valid && !t.done) {
        const QJsonObject before = snapshot(kTask, taskId, {QStringLiteral("due"), QStringLiteral("doneAt")});
        Task moved = t;
        moved.dueDate = r.afterCompletion(t.dueDate, today);
        QJsonObject f;
        f.insert(QStringLiteral("due"), moved.dueString());
        f.insert(QStringLiteral("doneAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        put(kTask, taskId, f);
        return before;
    }
    const QJsonObject before = snapshot(kTask, taskId, {QStringLiteral("done"), QStringLiteral("doneAt")});
    QJsonObject f;
    f.insert(QStringLiteral("done"), !t.done);
    f.insert(QStringLiteral("doneAt"), t.done ? QString() : QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    put(kTask, taskId, f);
    return before;
}

bool Store::remove(const QString &kind, const QString &id)
{
    // The entity and (for a task) all its subtasks, written as one batch so
    // listeners rebuild once.
    QStringList ids{id};
    if (kind == kTask) {
        ensureCache();
        for (qsizetype i = 0; i < ids.size(); ++i) { // ids grows while we walk it
            for (const Task &c : m_childCache.value(ids.at(i))) {
                if (!ids.contains(c.id)) {
                    ids << c.id;
                }
            }
        }
    }
    QJsonObject f;
    f.insert(kDeleted, true);
    beginBatch();
    bool ok = true;
    for (const QString &x : std::as_const(ids)) {
        ok = put(kind, x, f) && ok;
    }
    endBatch();
    return ok;
}

void Store::endBatch()
{
    if (m_batch > 0 && --m_batch == 0 && m_batchDirty) {
        m_batchDirty = false;
        emit changed();
    }
}

const Store::Entity *Store::entity(const QString &kind, const QString &id) const
{
    const auto it = m_entities.constFind(key(kind, id));
    return it == m_entities.cend() ? nullptr : &*it;
}

Task Store::toTask(const QString &id, const Entity &e) const
{
    auto v = [&e](const char *name) { return e.f.value(QLatin1String(name)).v; };
    Task t;
    t.id = id;
    t.title = v("title").toString();
    t.notes = v("notes").toString();
    t.project = v("project").toString();
    t.parent = v("parent").toString();
    for (const QJsonValue &l : v("labels").toArray()) {
        t.labels << l.toString();
    }
    t.priority = std::clamp(v("priority").toInt(4), 1, 4);
    parseDueInto(v("due").toString(), &t.dueDate, &t.dueTime);
    t.recur = v("recur").toString();
    t.done = v("done").toBool();
    t.doneAt = QDateTime::fromString(v("doneAt").toString(), Qt::ISODate);
    t.created = QDateTime::fromString(v("created").toString(), Qt::ISODate);
    t.order = v("order").toDouble();
    return t;
}

void Store::ensureCache() const
{
    if (m_cacheRevision == m_revision) {
        return;
    }
    m_taskCache.clear();
    m_childCache.clear();
    m_projectCache.clear();
    m_labelCache.clear();
    const QString tp = kTask + QLatin1Char(':');
    const QString pp = kProject + QLatin1Char(':');
    const QString lp = kLabel + QLatin1Char(':');
    for (auto it = m_entities.cbegin(); it != m_entities.cend(); ++it) {
        const QString &k = it.key();
        const Entity &e = *it;
        const QString kind = k.startsWith(tp) ? kTask : k.startsWith(pp) ? kProject : kLabel;
        if (e.deleted()) {
            continue;
        }
        if (kind == kTask) {
            if (e.f.contains(QStringLiteral("title"))) {
                m_taskCache << toTask(k.mid(tp.size()), e);
            }
        } else if (kind == kProject) {
            Project p;
            p.id = k.mid(pp.size());
            p.name = e.f.value(QStringLiteral("name")).v.toString();
            p.color = e.f.value(QStringLiteral("color")).v.toString();
            p.order = e.f.value(QStringLiteral("order")).v.toDouble();
            if (!p.name.isEmpty()) {
                m_projectCache << p;
            }
        } else {
            Label l;
            l.id = k.mid(lp.size());
            l.name = e.f.value(QStringLiteral("name")).v.toString();
            l.color = e.f.value(QStringLiteral("color")).v.toString();
            if (!l.name.isEmpty()) {
                m_labelCache << l;
            }
        }
    }
    std::sort(m_taskCache.begin(), m_taskCache.end(), [](const Task &a, const Task &b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    for (const Task &t : std::as_const(m_taskCache)) {
        if (!t.parent.isEmpty()) {
            m_childCache[t.parent] << t;
        }
    }
    std::sort(m_projectCache.begin(), m_projectCache.end(), [](const Project &a, const Project &b) {
        return a.order != b.order ? a.order < b.order : a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    std::sort(m_labelCache.begin(), m_labelCache.end(),
              [](const Label &a, const Label &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    m_cacheRevision = m_revision;
}

QList<Task> Store::tasks() const
{
    ensureCache();
    return m_taskCache;
}

QList<Task> Store::children(const QString &parentId) const
{
    ensureCache();
    return m_childCache.value(parentId);
}

bool Store::task(const QString &id, Task *out) const
{
    const Entity *e = entity(kTask, id);
    if (!e || e->deleted() || !e->f.contains(QStringLiteral("title"))) {
        return false;
    }
    *out = toTask(id, *e);
    return true;
}

QList<Project> Store::projects() const
{
    ensureCache();
    return m_projectCache;
}

QList<Label> Store::labels() const
{
    ensureCache();
    return m_labelCache;
}

QString Store::projectName(const QString &id) const
{
    const Entity *e = entity(kProject, id);
    return e && !e->deleted() ? e->f.value(QStringLiteral("name")).v.toString() : QString();
}

QString Store::labelName(const QString &id) const
{
    const Entity *e = entity(kLabel, id);
    return e && !e->deleted() ? e->f.value(QStringLiteral("name")).v.toString() : QString();
}

QString Store::findProject(const QString &name) const
{
    const QString n = normalizedName(name);
    for (const Project &p : projects()) {
        if (normalizedName(p.name) == n) {
            return p.id;
        }
    }
    return {};
}

QString Store::findLabel(const QString &name) const
{
    const QString n = normalizedName(name);
    for (const Label &l : labels()) {
        if (normalizedName(l.name) == n) {
            return l.id;
        }
    }
    return {};
}

double Store::nextOrder(const QString &kind) const
{
    return m_maxOrder.value(kind, 0) + 1;
}

qint64 Store::compactOwnLog()
{
    if (m_dir.isEmpty()) {
        return 0;
    }
    reload(); // see every winner we can before dropping anything
    QFile in(ownLogPath());
    if (!in.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const QByteArray all = in.readAll();
    in.close();
    QByteArray out;
    for (const QByteArray &raw : all.split('\n')) {
        Op op;
        if (raw.trimmed().isEmpty() || !Op::fromJsonLine(raw.trimmed(), &op) || op.device != m_device) {
            continue;
        }
        const Entity *e = entity(op.kind, op.id);
        QJsonObject keep;
        for (auto it = op.fields.begin(); it != op.fields.end(); ++it) {
            const FieldVal cur = e ? e->f.value(it.key()) : FieldVal{};
            // Keep the field only if this op is still its winner.
            if (cur.ts == op.ts && cur.dev == op.device) {
                keep.insert(it.key(), it.value());
            }
        }
        if (!keep.isEmpty()) {
            op.fields = keep;
            out += op.toJsonLine();
        }
    }
    if (out.size() >= all.size()) {
        return 0;
    }
    QSaveFile f(ownLogPath());
    if (!f.open(QIODevice::WriteOnly) || f.write(out) != out.size() || !f.commit()) {
        return 0;
    }
    const QFileInfo fi(ownLogPath());
    m_stamps.insert(fi.filePath(), stampFor(fi.filePath(), fi.size()));
    return all.size() - out.size();
}

} // namespace ztodo
