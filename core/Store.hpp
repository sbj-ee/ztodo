#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QFileSystemWatcher;
class QTimer;

namespace ztodo {

// ---------------------------------------------------------------------------
// Data folder layout (docs/STORAGE.md), shared by every device through Box:
//
//   <data folder>/ztodo.json             {"format":"ztodo","version":1}
//   <data folder>/devices/<device>.jsonl  one append-only op log per device
//
// A device only ever writes its own log, so a sync client (Box Drive, rclone
// bisync) never has two writers for one file. Every device reads every log and
// merges them: each field of each entity is last-writer-wins by (timestamp,
// device id), which gives the same result whatever order the logs arrive in.
// Conflict copies a sync client makes ("x.jsonl.conflict1", "x (conflicted
// copy).jsonl") are read too: merging is idempotent, so duplicates are harmless.
// ---------------------------------------------------------------------------

// One change: set some fields of one entity.
struct Op {
    qint64 ts = 0;  // hybrid clock: max(wall ms, last seen + 1); 0 .. 2^53
    QString device; // writer
    QString kind;   // "task" | "project" | "label"
    QString id;
    QJsonObject fields;

    QByteArray toJsonLine() const; // with trailing '\n'
    static bool fromJsonLine(const QByteArray &line, Op *out);
};

struct Task {
    QString id;
    QString title;
    QString notes;
    QString project; // project id; empty = Inbox
    QString parent;  // parent task id (subtask); empty = top level
    QStringList labels; // label ids
    int priority = 4;   // 1 (highest, red) .. 4 (none), as Todoist's p1..p4
    QDate dueDate;      // invalid = no date
    QTime dueTime;      // invalid = all day
    QString recur;      // "every week", ... (Recurrence.hpp); empty = once
    bool done = false;
    QDateTime doneAt;
    QDateTime created;
    double order = 0;

    bool hasDue() const { return dueDate.isValid(); }
    QString dueString() const; // "2026-11-12" or "2026-11-12T17:00"
};

// One completion of a task (kind "completion"), written alongside the task's
// done/doneAt change. Completing a recurring task only moves its date, so
// these records are the only full history of what was done when; the
// productivity stats (Stats.hpp) count them. Never edited, only tombstoned
// when the completion is reopened or undone.
struct Completion {
    QString id;
    QString task;    // task id
    QString project; // the task's project at the time (empty = Inbox)
    QDateTime at;    // UTC; equals the task's doneAt written with it
};

// "Put these old field values back" for one entity (undo).
struct FieldRestore {
    QString kind;
    QString id;
    QJsonObject before;
};

struct Project {
    QString id;
    QString name;
    QString color; // "#rrggbb"
    double order = 0;
};

struct Label {
    QString id;
    QString name;
    QString color;
};

QString parseDueInto(const QString &s, QDate *date, QTime *time); // inverse of dueString(); returns s
QString newId(); // random 128-bit id, url-safe

class Store : public QObject {
    Q_OBJECT
public:
    explicit Store(QObject *parent = nullptr);
    ~Store() override;

    // Opens (creating if needed) the data folder. deviceId names this device's
    // log; it must be stable per installation and unique across devices.
    bool open(const QString &dir, const QString &deviceId, QString *error = nullptr);
    void close();
    bool isOpen() const { return !m_dir.isEmpty(); }
    QString dir() const { return m_dir; }
    QString deviceId() const { return m_device; }
    QString ownLogPath() const;

    // Re-reads changed logs. Returns true if the merged state changed.
    bool reload();
    // Watch the folder (file watcher + polling, since watchers miss changes on
    // FUSE / File Provider mounts). Emits changed() after external updates.
    void setPollInterval(int ms);

    // --- writes (append to own log, apply, emit changed()) ---
    bool put(const QString &kind, const QString &id, const QJsonObject &fields);
    QString addTask(const Task &t);    // returns id
    QString addProject(const QString &name, const QString &color = {});
    QString addLabel(const QString &name, const QString &color = {});
    bool setTaskFields(const QString &id, const QJsonObject &fields) { return put(QStringLiteral("task"), id, fields); }
    // Completes a task: a recurring one moves to its next date instead.
    // Returns the fields that were changed with their previous values (undo).
    // Completing also writes a Completion record; reopening tombstones the
    // task's latest one. `other` (optional) receives the undo entries for
    // those records.
    QJsonObject complete(const QString &taskId, const QDate &today = QDate::currentDate(),
                         QList<FieldRestore> *other = nullptr);
    bool remove(const QString &kind, const QString &id); // tombstone (deleted: true); a task's subtasks too

    // Group several writes into one changed() signal (nestable).
    void beginBatch() { ++m_batch; }
    void endBatch();

    // Current values of the given fields (for undo); missing fields map to null.
    QJsonObject snapshot(const QString &kind, const QString &id, const QStringList &fields) const;

    // --- reads (merged, without deleted entities) ---
    QList<Task> tasks() const;       // by order; cached per revision (cheap to call)
    QList<Task> children(const QString &parentId) const; // direct subtasks, by order
    QList<Project> projects() const; // by order
    QList<Label> labels() const;     // by name
    QList<Completion> completions() const; // by time, oldest first
    bool task(const QString &id, Task *out) const;
    QString projectName(const QString &id) const;
    QString labelName(const QString &id) const;
    QString findProject(const QString &name) const; // id, case-insensitive; empty if none
    QString findLabel(const QString &name) const;
    double nextOrder(const QString &kind) const;
    quint64 revision() const { return m_revision; }

    // Rewrites this device's log without ops that every reader would ignore
    // (fields superseded by a newer write). Safe: a dropped op is only dropped
    // once its winner, in some log, has been read. Returns bytes saved.
    qint64 compactOwnLog();

    // Tests.
    void setClockForTests(std::function<qint64()> now) { m_now = std::move(now); }
    static int logFileCount(const QString &dir);

signals:
    void changed();
    void error(const QString &message);

private:
    struct FieldVal {
        QJsonValue v;
        qint64 ts = 0;
        QString dev;
    };
    struct Entity {
        QHash<QString, FieldVal> f;
        bool deleted() const { return f.value(QStringLiteral("deleted")).v.toBool(); }
    };
    struct FileStamp {
        qint64 size = -1;
        QDateTime mtime;
        // Incremental reading: bytes up to `offset` (a line boundary) are
        // already applied. `tail` holds the bytes just before `offset`; if
        // they have changed (the file was rewritten, e.g. compacted) the
        // whole file is read again, which is always safe.
        qint64 offset = 0;
        QByteArray tail;
    };

    bool applyOp(const Op &op);
    bool readFile(const QString &path, FileStamp *stamp);
    static FileStamp stampFor(const QString &path, qint64 consumed);
    void ensureCache() const;
    qint64 tick();
    static QString key(const QString &kind, const QString &id) { return kind + QLatin1Char(':') + id; }
    const Entity *entity(const QString &kind, const QString &id) const;
    Task toTask(const QString &id, const Entity &e) const;
    void scheduleReload();

    QString m_dir;
    QString m_device;
    QHash<QString, Entity> m_entities;
    QHash<QString, FileStamp> m_stamps;
    qint64 m_clock = 0;
    quint64 m_revision = 0;
    std::function<qint64()> m_now;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_poll = nullptr;
    QTimer *m_debounce = nullptr;
    int m_batch = 0;           // >0: put() does not emit changed()
    bool m_batchDirty = false;

    // Derived lists, rebuilt lazily when m_revision moves.
    mutable quint64 m_cacheRevision = ~quint64(0);
    mutable QList<Task> m_taskCache;
    mutable QHash<QString, QList<Task>> m_childCache;
    mutable QList<Project> m_projectCache;
    mutable QList<Label> m_labelCache;
    mutable QList<Completion> m_completionCache;
    QHash<QString, double> m_maxOrder; // per kind, deleted entities included; kept by applyOp()
};

} // namespace ztodo
