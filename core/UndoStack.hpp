#pragma once

#include "Store.hpp"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace ztodo {

// Undo history. Every entry is a list of "put these old field values back"
// changes, so undoing is just more writes to the Store (which sync like any
// other change). No widgets: usable and testable without a GUI.
class UndoStack {
public:
    struct Change {
        QString kind;
        QString id;
        QJsonObject before; // field values to restore (null = field was unset)
    };

    explicit UndoStack(int limit = 100) : m_limit(limit) {}

    void push(const QString &text, const QList<Change> &changes);
    // Adds changes to the newest entry (a multi-step action recorded in parts).
    void extendLast(const QList<Change> &changes);
    // Restores the newest entry (last change first, as one store batch).
    // Returns its text; empty if there was nothing to undo.
    QString undo(Store *store);
    void clear() { m_entries.clear(); }
    bool canUndo() const { return !m_entries.isEmpty(); }
    QString lastText() const { return m_entries.isEmpty() ? QString() : m_entries.last().text; }
    qsizetype size() const { return m_entries.size(); }

private:
    struct Entry {
        QString text;
        QList<Change> changes;
    };
    QList<Entry> m_entries;
    int m_limit;
};

} // namespace ztodo
