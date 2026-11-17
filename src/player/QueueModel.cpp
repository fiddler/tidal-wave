#include "QueueModel.h"

int QueueModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant QueueModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const Row &r = m_rows.at(index.row());
    if (role == TrackRole)      return r.track;
    if (role == QueueIndexRole) return r.queueIndex;
    return {};
}

QHash<int, QByteArray> QueueModel::roleNames() const {
    return { { TrackRole, "track" }, { QueueIndexRole, "queueIndex" } };
}

// Rows are matched on the track, not on the queue index: the index is what an
// edit changes. A queue can hold the same track twice; the diff then names one
// of several equally true edits, and the rows end up right either way.
bool QueueModel::sameTrack(const Row &a, const Row &b) {
    return a.track.value(QStringLiteral("id")) == b.track.value(QStringLiteral("id"))
        && a.track.value(QStringLiteral("localPath")) == b.track.value(QStringLiteral("localPath"));
}

// After the structure matches, every row takes its new contents. Queue indices
// shift under any edit, so this is not limited to the rows that moved.
void QueueModel::refreshRows(const QList<Row> &next) {
    QList<int> changed;
    for (int i = 0; i < next.size(); ++i) {
        if (m_rows[i].queueIndex != next[i].queueIndex || m_rows[i].track != next[i].track)
            changed << i;
        m_rows[i] = next[i];
    }
    if (changed.isEmpty()) return;
    emit dataChanged(index(changed.first()), index(changed.last()));
}

void QueueModel::sync(const QList<Row> &next) {
    const int oldN = int(m_rows.size());
    const int newN = int(next.size());

    // The first and last rows that differ bound whatever happened.
    int head = 0;
    while (head < oldN && head < newN && sameTrack(m_rows[head], next[head])) ++head;
    int tail = 0;
    while (tail < oldN - head && tail < newN - head
           && sameTrack(m_rows[oldN - 1 - tail], next[newN - 1 - tail])) ++tail;

    if (newN == oldN) {
        const int lo = head, hi = oldN - 1 - tail;
        if (lo > hi) { refreshRows(next); return; }       // same tracks, same order

        // One row carried from one end of the differing span to the other.
        const auto shifted = [&](int from, int to) {
            QList<Row> probe = m_rows;
            probe.move(from, to);
            for (int i = lo; i <= hi; ++i)
                if (!sameTrack(probe[i], next[i])) return false;
            return true;
        };
        if (shifted(lo, hi)) {
            beginMoveRows({}, lo, lo, {}, hi + 1);
            m_rows.move(lo, hi);
            endMoveRows();
            refreshRows(next);
            return;
        }
        if (shifted(hi, lo)) {
            beginMoveRows({}, hi, hi, {}, lo);
            m_rows.move(hi, lo);
            endMoveRows();
            refreshRows(next);
            return;
        }
    } else if (newN < oldN && head + tail >= newN) {
        // A run removed: everything before and after it survives in place.
        const int first = head, count = oldN - newN;
        beginRemoveRows({}, first, first + count - 1);
        m_rows.remove(first, count);
        endRemoveRows();
        refreshRows(next);
        return;
    } else if (newN > oldN && head + tail >= oldN) {
        const int first = head, count = newN - oldN;
        beginInsertRows({}, first, first + count - 1);
        for (int i = 0; i < count; ++i) m_rows.insert(first + i, next[first + i]);
        endInsertRows();
        refreshRows(next);
        return;
    }

    beginResetModel();
    m_rows = next;
    endResetModel();
}
