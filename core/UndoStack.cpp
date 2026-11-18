#include "UndoStack.hpp"

namespace ztodo {

void UndoStack::push(const QString &text, const QList<Change> &changes)
{
    m_entries << Entry{text, changes};
    while (m_entries.size() > m_limit) {
        m_entries.removeFirst();
    }
}

void UndoStack::extendLast(const QList<Change> &changes)
{
    if (!m_entries.isEmpty()) {
        m_entries.last().changes << changes;
    }
}

QString UndoStack::undo(Store *store)
{
    if (m_entries.isEmpty()) {
        return {};
    }
    const Entry e = m_entries.takeLast();
    store->beginBatch();
    for (qsizetype i = e.changes.size() - 1; i >= 0; --i) {
        const Change &c = e.changes.at(i);
        store->put(c.kind, c.id, c.before);
    }
    store->endBatch();
    return e.text.isEmpty() ? QStringLiteral("change") : e.text;
}

} // namespace ztodo
