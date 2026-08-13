#include "OfflineManager.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QDebug>

#include "DashFetcher.h"
#include "ImageProvider.h"
#include "TidalClient.h"

namespace {

// Rough delivered bytes per second of audio for each quality tier, used only
// for the size estimate shown before pinning.
qint64 bytesPerSecond(AudioQuality q) {
    switch (q) {
    case AudioQuality::Low96k:        return  12'000;
    case AudioQuality::Low320k:       return  40'000;
    case AudioQuality::Lossless:      return 120'000;
    case AudioQuality::HiResLossless: return 350'000;
    }
    return 120'000;
}

QString humanDuration(qint64 seconds) {
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    if (h > 0) return QStringLiteral("%1 h %2 min").arg(h).arg(m);
    if (m > 0) return QStringLiteral("%1 min").arg(m);
    return QStringLiteral("under a minute");
}

} // namespace

OfflineManager::OfflineManager(TidalClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{
    bool ok = false;
    const int pace = qEnvironmentVariableIntValue("TIDAL_WAVE_OFFLINE_PACE_MS", &ok);
    if (ok && pace >= 0) m_paceOverrideMs = pace;

    openDatabase();
    resetInterrupted();
    // Resume an interrupted sync shortly after launch, once the app is idle.
    // The artwork backfill covers tracks cached before covers were pre-fetched
    // (it is a no-op when the cache already has them).
    QTimer::singleShot(5000, this, [this]() {
        QSqlQuery q(QStringLiteral("SELECT track_id FROM tracks WHERE state='done'"), m_db);
        while (q.next()) cacheCoverArt(q.value(0).toLongLong());
        startNext();
    });
}

OfflineManager::~OfflineManager() {
    abortJob();
}

// ─────────────────────────── storage ───────────────────────────

void OfflineManager::openDatabase() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_mediaDir = dir + QStringLiteral("/offline");
    QDir().mkpath(m_mediaDir);

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("offline-store"));
    m_db.setDatabaseName(dir + QStringLiteral("/offline.db"));
    if (!m_db.open()) {
        emit error(tr("Could not open the offline store: %1").arg(m_db.lastError().text()));
        return;
    }
    QSqlQuery(QStringLiteral("PRAGMA foreign_keys = ON"), m_db);
    createSchema();
}

void OfflineManager::createSchema() {
    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS playlists ("
        " uuid TEXT PRIMARY KEY,"
        " title TEXT NOT NULL,"
        " cover TEXT,"
        " pinned_at INTEGER NOT NULL DEFAULT 0)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS items ("
        " uuid     TEXT    NOT NULL REFERENCES playlists(uuid) ON DELETE CASCADE,"
        " position INTEGER NOT NULL,"
        " track_id INTEGER NOT NULL)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_offline_items ON items(uuid, position)"));
    // meta is the trackToMap-shaped map as JSON, so the offline fallback can
    // rebuild exactly what the playlist page renders. state: pending|done|error.
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS tracks ("
        " track_id INTEGER PRIMARY KEY,"
        " meta  TEXT NOT NULL,"
        " path  TEXT,"
        " bytes INTEGER NOT NULL DEFAULT 0,"
        " tier  TEXT,"
        " state TEXT NOT NULL DEFAULT 'pending')"));
}

void OfflineManager::resetInterrupted() {
    if (!m_db.isOpen()) return;
    // Errors get a fresh chance every launch, and a "done" row whose file has
    // vanished (user cleared the cache dir) goes back to pending.
    QSqlQuery(QStringLiteral("UPDATE tracks SET state='pending' WHERE state='error'"), m_db);
    QSqlQuery q(QStringLiteral("SELECT track_id, path FROM tracks WHERE state='done'"), m_db);
    QList<qint64> missing;
    while (q.next())
        if (!QFile::exists(q.value(1).toString()))
            missing << q.value(0).toLongLong();
    for (qint64 id : missing) {
        QSqlQuery u(m_db);
        u.prepare(QStringLiteral("UPDATE tracks SET state='pending', path=NULL, bytes=0 WHERE track_id=?"));
        u.addBindValue(id);
        u.exec();
    }
}

// ─────────────────────────── QML API ───────────────────────────

QVariantMap OfflineManager::estimate(const QVariantList &tracks) const {
    qint64 seconds = 0;
    int count = 0;
    for (const QVariant &v : tracks) {
        const QVariantMap m = v.toMap();
        if (!m.value(QStringLiteral("available"), true).toBool()) continue;
        seconds += m.value(QStringLiteral("duration")).toInt();
        ++count;
    }
    const qint64 bytes = seconds * bytesPerSecond(m_client->audioQuality());
    QVariantMap r;
    r.insert(QStringLiteral("count"),   count);
    r.insert(QStringLiteral("bytes"),   bytes);
    r.insert(QStringLiteral("seconds"), seconds);
    r.insert(QStringLiteral("sizeStr"), QLocale().formattedDataSize(bytes, 1));
    // Downloads run at listening speed, so the wall-clock estimate is simply
    // the playlist's own duration.
    r.insert(QStringLiteral("timeStr"), humanDuration(seconds));
    return r;
}

void OfflineManager::pin(const QString &uuid, const QString &title,
                         const QString &cover, const QVariantList &tracks) {
    if (!m_db.isOpen() || uuid.isEmpty() || tracks.isEmpty()) return;

    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO playlists(uuid, title, cover, pinned_at) VALUES(?,?,?,strftime('%s','now'))"
        " ON CONFLICT(uuid) DO UPDATE SET title=excluded.title, cover=excluded.cover"));
    q.addBindValue(uuid); q.addBindValue(title); q.addBindValue(cover);
    q.exec();

    q.prepare(QStringLiteral("DELETE FROM items WHERE uuid=?"));
    q.addBindValue(uuid);
    q.exec();

    int position = 0;
    for (const QVariant &v : tracks) {
        const QVariantMap m = v.toMap();
        const qint64 id = m.value(QStringLiteral("id")).toLongLong();
        // Retired tracks cannot be streamed, so they cannot be cached either;
        // leaving them out keeps the playlist from being stuck "syncing".
        if (id <= 0 || !m.value(QStringLiteral("available"), true).toBool()) continue;

        q.prepare(QStringLiteral("INSERT INTO items(uuid, position, track_id) VALUES(?,?,?)"));
        q.addBindValue(uuid); q.addBindValue(position++); q.addBindValue(id);
        q.exec();

        const QString meta = QString::fromUtf8(
            QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact));
        q.prepare(QStringLiteral(
            "INSERT INTO tracks(track_id, meta) VALUES(?,?)"
            " ON CONFLICT(track_id) DO UPDATE SET meta=excluded.meta"));
        q.addBindValue(id); q.addBindValue(meta);
        q.exec();
    }
    // Re-pinning is also the "retry" gesture: failed tracks get a fresh chance.
    q.prepare(QStringLiteral(
        "UPDATE tracks SET state='pending' WHERE state='error'"
        " AND track_id IN (SELECT track_id FROM items WHERE uuid=?)"));
    q.addBindValue(uuid);
    q.exec();
    m_db.commit();

    emit playlistChanged(uuid);
    startNext();
}

void OfflineManager::unpin(const QString &uuid) {
    if (!m_db.isOpen()) return;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM playlists WHERE uuid=?"));
    q.addBindValue(uuid);
    q.exec();

    // Drop cached audio no remaining pinned playlist references.
    QList<qint64> orphans;
    QSqlQuery sel(QStringLiteral(
        "SELECT track_id, path FROM tracks"
        " WHERE track_id NOT IN (SELECT track_id FROM items)"), m_db);
    while (sel.next()) {
        orphans << sel.value(0).toLongLong();
        const QString path = sel.value(1).toString();
        if (!path.isEmpty()) QFile::remove(path);
    }
    for (qint64 id : orphans) {
        if (m_job && m_job->id == id) abortJob();
        QSqlQuery del(m_db);
        del.prepare(QStringLiteral("DELETE FROM tracks WHERE track_id=?"));
        del.addBindValue(id);
        del.exec();
    }

    emit playlistChanged(uuid);
    // If the in-flight download was the one just removed, move on to whatever
    // other pinned playlists still need.
    startNext();
}

QVariantMap OfflineManager::status(const QString &uuid) const {
    QVariantMap r;
    r.insert(QStringLiteral("state"), QStringLiteral("none"));
    r.insert(QStringLiteral("done"),  0);
    r.insert(QStringLiteral("total"), 0);
    if (!m_db.isOpen()) return r;

    QSqlQuery p(m_db);
    p.prepare(QStringLiteral("SELECT 1 FROM playlists WHERE uuid=?"));
    p.addBindValue(uuid);
    p.exec();
    if (!p.next()) return r;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*),"
        " SUM(CASE WHEN t.state='done'  THEN 1 ELSE 0 END),"
        " SUM(CASE WHEN t.state='error' THEN 1 ELSE 0 END)"
        " FROM items i JOIN tracks t ON t.track_id = i.track_id WHERE i.uuid=?"));
    q.addBindValue(uuid);
    q.exec();
    if (!q.next()) return r;

    const int total  = q.value(0).toInt();
    const int done   = q.value(1).toInt();
    const int failed = q.value(2).toInt();
    r.insert(QStringLiteral("done"),  done);
    r.insert(QStringLiteral("total"), total);
    if (total > 0 && done == total)
        r.insert(QStringLiteral("state"), QStringLiteral("offline"));
    else if (failed > 0 && done + failed == total)
        r.insert(QStringLiteral("state"), QStringLiteral("error"));
    else
        r.insert(QStringLiteral("state"), QStringLiteral("syncing"));
    return r;
}

QVariantList OfflineManager::cachedTracks(const QString &uuid) const {
    QVariantList list;
    if (!m_db.isOpen()) return list;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT t.meta FROM items i JOIN tracks t ON t.track_id = i.track_id"
        " WHERE i.uuid=? ORDER BY i.position"));
    q.addBindValue(uuid);
    q.exec();
    while (q.next()) {
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(0).toByteArray());
        if (doc.isObject()) list.append(doc.object().toVariantMap());
    }
    return list;
}

QVariantList OfflineManager::pinnedPlaylists() const {
    QVariantList list;
    if (!m_db.isOpen()) return list;
    QSqlQuery q(QStringLiteral(
        "SELECT uuid, title, cover FROM playlists ORDER BY pinned_at DESC"), m_db);
    while (q.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("uuid"),  q.value(0).toString());
        m.insert(QStringLiteral("title"), q.value(1).toString());
        m.insert(QStringLiteral("cover"), q.value(2).toString());
        list.append(m);
    }
    return list;
}

// ────────────────────── player intercept ───────────────────────

QString OfflineManager::localPathFor(qint64 trackId) const {
    if (!m_db.isOpen()) return {};
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT path FROM tracks WHERE track_id=? AND state='done'"));
    q.addBindValue(trackId);
    q.exec();
    if (!q.next()) return {};
    const QString path = q.value(0).toString();
    return QFile::exists(path) ? path : QString();
}

QString OfflineManager::tierFor(qint64 trackId) const {
    if (!m_db.isOpen()) return {};
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT tier FROM tracks WHERE track_id=?"));
    q.addBindValue(trackId);
    q.exec();
    return q.next() ? q.value(0).toString() : QString();
}

// ───────────────────────── download queue ──────────────────────

void OfflineManager::startNext() {
    if (!m_db.isOpen() || m_job || m_waiting) return;

    // Next pending track in pin order, then playlist order.
    QSqlQuery q(QStringLiteral(
        "SELECT t.track_id, t.meta FROM items i"
        " JOIN tracks t ON t.track_id = i.track_id"
        " WHERE t.state='pending'"
        " GROUP BY t.track_id ORDER BY MIN(i.rowid) LIMIT 1"), m_db);
    if (!q.next()) return;   // nothing to do

    const qint64 id = q.value(0).toLongLong();
    const QJsonDocument doc = QJsonDocument::fromJson(q.value(1).toByteArray());
    const int duration = doc.object().value(QStringLiteral("duration")).toInt();

    m_job = new Job;
    m_job->id = id;
    m_job->durationSecs = duration;
    m_job->started.start();

    qInfo() << "[offline] fetching track" << id;
    m_client->fetchStreamManifest(id, [this, id](StreamManifest manifest, QString err) {
        if (!m_job || m_job->id != id) return;   // aborted meanwhile
        if (!err.isEmpty()) { completeJob({}, 0, err); return; }

        const QString tier = manifest.codec;
        if (manifest.type == StreamManifest::BTS) {
            m_job->reply = m_client->fetchRaw(QUrl(manifest.url),
                [this, id, tier](QByteArray data, QString err2) {
                    if (!m_job || m_job->id != id) return;
                    m_job->reply = nullptr;
                    if (!err2.isEmpty() || data.isEmpty()) {
                        completeJob({}, 0, err2.isEmpty() ? QStringLiteral("empty stream") : err2);
                        return;
                    }
                    const QString target = m_mediaDir + QStringLiteral("/%1.mp4").arg(id);
                    QFile f(target);
                    if (!f.open(QIODevice::WriteOnly)) {
                        completeJob({}, 0, QStringLiteral("cannot write cache file"));
                        return;
                    }
                    f.write(data);
                    f.close();
                    completeJob(tier, f.size(), {});
                });
        } else {
            auto *fetcher = new DashFetcher(m_client, manifest.url, this);
            if (!fetcher->isValid()) {
                delete fetcher;
                completeJob({}, 0, QStringLiteral("unreadable stream manifest"));
                return;
            }
            m_job->dash = fetcher;
            connect(fetcher, &DashFetcher::finished, this,
                [this, id, tier](QTemporaryFile *file, const QString &err2) {
                    if (!m_job || m_job->id != id) {
                        if (file) { file->remove(); delete file; }
                        return;
                    }
                    if (m_job->dash) { m_job->dash->deleteLater(); m_job->dash = nullptr; }
                    if (!file) { completeJob({}, 0, err2); return; }

                    const QString target = m_mediaDir + QStringLiteral("/%1.mp4").arg(id);
                    file->setAutoRemove(false);
                    const QString tmpPath = file->fileName();
                    delete file;   // keep the bytes on disk
                    QFile::remove(target);
                    if (!QFile::rename(tmpPath, target)) {
                        // /tmp can be a different volume — fall back to a copy.
                        if (!QFile::copy(tmpPath, target)) {
                            QFile::remove(tmpPath);
                            completeJob({}, 0, QStringLiteral("cannot write cache file"));
                            return;
                        }
                        QFile::remove(tmpPath);
                    }
                    completeJob(tier, QFile(target).size(), {});
                });
            fetcher->start();
        }
    });
}

void OfflineManager::completeJob(const QString &tier, qint64 bytes, const QString &err) {
    if (!m_job) return;
    const qint64 id       = m_job->id;
    const int    duration = m_job->durationSecs;
    const int    elapsed  = int(m_job->started.elapsed());
    const int    attempts = ++m_job->attempts;

    if (!err.isEmpty()) {
        qWarning() << "[offline] track" << id << "failed (attempt" << attempts << "):" << err;
        if (attempts < 3) {
            // Transient failures get another try after a pause; the job stays
            // current so abort/unpin still reaches it.
            QTimer::singleShot(30'000, this, [this, id]() {
                if (!m_job || m_job->id != id) return;
                Job *retry = m_job;
                m_job = nullptr;
                const int keepAttempts = retry->attempts;
                delete retry;
                // Re-enter through startNext so the flow stays single-pathed;
                // the row is still 'pending' so it is picked first again.
                startNext();
                if (m_job) m_job->attempts = keepAttempts;
            });
            return;
        }
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("UPDATE tracks SET state='error' WHERE track_id=?"));
        q.addBindValue(id);
        q.exec();
        delete m_job;
        m_job = nullptr;
        notifyPlaylistsHolding(id);
        scheduleNext(elapsed, 0);
        return;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE tracks SET state='done', path=?, bytes=?, tier=? WHERE track_id=?"));
    q.addBindValue(m_mediaDir + QStringLiteral("/%1.mp4").arg(id));
    q.addBindValue(bytes);
    q.addBindValue(tier);
    q.addBindValue(id);
    q.exec();

    qInfo() << "[offline] track" << id << "cached," << bytes << "bytes," << tier;
    cacheCoverArt(id);
    delete m_job;
    m_job = nullptr;
    notifyPlaylistsHolding(id);
    scheduleNext(elapsed, duration);
}

void OfflineManager::cacheCoverArt(qint64 trackId) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT meta FROM tracks WHERE track_id=?"));
    q.addBindValue(trackId);
    q.exec();
    if (!q.next()) return;
    const QJsonObject meta = QJsonDocument::fromJson(q.value(0).toByteArray()).object();

    // The two sizes the playback UI actually renders. Best-effort and
    // fire-and-forget: a miss just means the cover streams next time.
    for (const auto &key : {QStringLiteral("coverUrl"), QStringLiteral("coverUrl80")}) {
        const QString u = meta.value(key).toString();
        if (u.isEmpty()) continue;
        const QUrl url(u);
        if (QFile::exists(TidalImageProvider::cachePathFor(url))) continue;
        m_client->fetchRaw(url, [url](QByteArray data, QString err) {
            if (err.isEmpty()) TidalImageProvider::storeInCache(url, data);
        });
    }
}

void OfflineManager::scheduleNext(int elapsedMs, int durationSecs) {
    // Listening-speed pacing: the next track starts when this one would have
    // finished playing. A failed track just gets a short courtesy gap.
    int delay = qMax(0, durationSecs * 1000 - elapsedMs);
    if (durationSecs == 0) delay = 5'000;
    if (m_paceOverrideMs >= 0) delay = m_paceOverrideMs;

    m_waiting = true;
    QTimer::singleShot(delay, this, [this]() {
        m_waiting = false;
        startNext();
    });
}

void OfflineManager::abortJob() {
    if (!m_job) return;
    if (m_job->reply) { m_job->reply->disconnect(); m_job->reply->abort(); m_job->reply->deleteLater(); }
    if (m_job->dash)  { m_job->dash->disconnect();  m_job->dash->abort();  m_job->dash->deleteLater(); }
    delete m_job;
    m_job = nullptr;
}

QList<QString> OfflineManager::playlistsHolding(qint64 trackId) const {
    QList<QString> uuids;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT DISTINCT uuid FROM items WHERE track_id=?"));
    q.addBindValue(trackId);
    q.exec();
    while (q.next()) uuids << q.value(0).toString();
    return uuids;
}

void OfflineManager::notifyPlaylistsHolding(qint64 trackId) {
    const auto uuids = playlistsHolding(trackId);
    for (const QString &uuid : uuids) emit playlistChanged(uuid);
}
