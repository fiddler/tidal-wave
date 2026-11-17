#pragma once
#include <QAbstractListModel>
#include <QVariantMap>

// The queue in play order, as a real model.
//
// The panel used to bind a ListView to a QVariantList, and assigning a new
// list is a model reset: every delegate destroyed and rebuilt on each track
// change, the scroll position thrown back to the top, and no way for a row to
// animate to its new place. sync() instead works out what one queue edit did
// — a move, a removed run, an inserted run — and reports exactly that.
class QueueModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { TrackRole = Qt::UserRole + 1, QueueIndexRole };

    // One row: the track map and its index in the player's own queue, which
    // differs from the row number while shuffled.
    struct Row {
        QVariantMap track;
        int         queueIndex = 0;
    };

    explicit QueueModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Brings the model to `next` with the smallest change that explains it.
    // Anything that is not a single move, removal or insertion is a reset.
    void sync(const QList<Row> &next);

private:
    static bool sameTrack(const Row &a, const Row &b);
    void refreshRows(const QList<Row> &next);

    QList<Row> m_rows;
};
