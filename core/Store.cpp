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
        FileStamp st{fi.size(), fi.lastModified()};
        const auto it = m_stamps.constFind(fi.filePath());
        if (it != m_stamps.cend() && it->size == st.size && it->mtime == st.mtime) {
            continue;
        }
        // Re-read the whole file: applying an op twice changes nothing, so
        // there's no bookkeeping of what was read before.
        if (readFile(fi.filePath())) {
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

bool Store::readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
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
    const QFileInfo fi(f.fileName());
    m_stamps.insert(fi.filePath(), FileStamp{fi.size(), fi.lastModified()});
    ++m_revision;
    emit changed();
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
    QJsonObject f;
    f.insert(kDeleted, true);
    if (!put(kind, id, f)) {
        return false;
    }
    if (kind == kTask) {
        for (const Task &t : tasks()) {
            if (t.parent == id) {
                remove(kTask, t.id);
            }
        }
    }
    return true;
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

QList<Task> Store::tasks() const
{
    QList<Task> out;
    const QString prefix = kTask + QLatin1Char(':');
    for (auto it = m_entities.cbegin(); it != m_entities.cend(); ++it) {
        if (it.key().startsWith(prefix) && !it->deleted() && it->f.contains(QStringLiteral("title"))) {
            out << toTask(it.key().mid(prefix.size()), *it);
        }
    }
    std::sort(out.begin(), out.end(), [](const Task &a, const Task &b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    return out;
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
    QList<Project> out;
    const QString prefix = kProject + QLatin1Char(':');
    for (auto it = m_entities.cbegin(); it != m_entities.cend(); ++it) {
        if (it.key().startsWith(prefix) && !it->deleted()) {
            Project p;
            p.id = it.key().mid(prefix.size());
            p.name = it->f.value(QStringLiteral("name")).v.toString();
            p.color = it->f.value(QStringLiteral("color")).v.toString();
            p.order = it->f.value(QStringLiteral("order")).v.toDouble();
            if (!p.name.isEmpty()) {
                out << p;
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const Project &a, const Project &b) {
        return a.order != b.order ? a.order < b.order : a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QList<Label> Store::labels() const
{
    QList<Label> out;
    const QString prefix = kLabel + QLatin1Char(':');
    for (auto it = m_entities.cbegin(); it != m_entities.cend(); ++it) {
        if (it.key().startsWith(prefix) && !it->deleted()) {
            Label l;
            l.id = it.key().mid(prefix.size());
            l.name = it->f.value(QStringLiteral("name")).v.toString();
            l.color = it->f.value(QStringLiteral("color")).v.toString();
            if (!l.name.isEmpty()) {
                out << l;
            }
        }
    }
    std::sort(out.begin(), out.end(),
              [](const Label &a, const Label &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    return out;
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
    double mx = 0;
    const QString prefix = kind + QLatin1Char(':');
    for (auto it = m_entities.cbegin(); it != m_entities.cend(); ++it) {
        if (it.key().startsWith(prefix)) {
            mx = std::max(mx, it->f.value(QStringLiteral("order")).v.toDouble());
        }
    }
    return mx + 1;
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
    m_stamps.insert(fi.filePath(), FileStamp{fi.size(), fi.lastModified()});
    return all.size() - out.size();
}

} // namespace ztodo
