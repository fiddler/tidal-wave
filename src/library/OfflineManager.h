#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QNetworkReply;
class DashFetcher;
class TidalClient;

// Offline copies of Tidal playlists ("pinning"). Exposed to QML as `offline`.
//
// A pinned playlist is stored as its track list plus one cached audio file per
// track in the app data directory. The audio is the same single-file shape
// playback already produces (BTS body or joined DASH fMP4), so no ffmpeg is
// involved and the player opens the file directly.
//
// Downloads are deliberately paced at listening speed: after each track the
// queue waits out the remainder of that track's duration before starting the
// next one. A full sync therefore takes about as long as playing the playlist
// once, so pinning never puts more load on Tidal than normal listening — and
// the playlist's own duration is the honest time estimate shown before pinning.
// TIDAL_WAVE_OFFLINE_PACE_MS overrides the gap (dev/testing only).
//
// The cache is bound to the app: files live under AppDataLocation, are named
// by track id, and are removed when the playlist is unpinned.
class OfflineManager : public QObject {
    Q_OBJECT
public:
    explicit OfflineManager(TidalClient *client, QObject *parent = nullptr);
    ~OfflineManager() override;

    // ── QML API ─────────────────────────────────────────
    // Size/time estimate for pinning `tracks` at the current playback quality:
    // {bytes, seconds, sizeStr, timeStr}.
    Q_INVOKABLE QVariantMap estimate(const QVariantList &tracks) const;
    // Pins (or re-syncs) a playlist. `tracks` are the trackToMap-shaped maps
    // the playlist page already holds, in playlist order.
    Q_INVOKABLE void pin(const QString &uuid, const QString &title,
                         const QString &cover, const QVariantList &tracks);
    // Removes the playlist and every cached file no other pinned playlist uses.
    Q_INVOKABLE void unpin(const QString &uuid);
    // {state: "none"|"syncing"|"offline"|"error", done, total}
    Q_INVOKABLE QVariantMap status(const QString &uuid) const;
    // Stored track maps in playlist order — the offline fallback when the
    // playlist cannot be fetched from the API.
    Q_INVOKABLE QVariantList cachedTracks(const QString &uuid) const;
    Q_INVOKABLE QVariantList pinnedPlaylists() const;

    // Re-fetch the artwork of every track already downloaded. Runs at start-up
    // for tracks cached before covers were pre-fetched, and again after the
    // art cache is emptied from Settings, so a pinned playlist does not sit
    // there with no covers until the next time it is online.
    Q_INVOKABLE void refetchCoverArt();

    // ── player intercept ────────────────────────────────
    // Path of the cached audio for a fully downloaded track, else empty.
    QString localPathFor(qint64 trackId) const;
    // Delivered tier of the cached audio ("LOSSLESS", …), for the quality badge.
    QString tierFor(qint64 trackId) const;

signals:
    // done/total moved for one playlist, or its state flipped.
    void playlistChanged(const QString &uuid);
    void error(const QString &message);

private:
    struct Job {
        qint64         id = 0;
        int            durationSecs = 0;
        QNetworkReply *reply = nullptr;
        DashFetcher   *dash  = nullptr;
        QElapsedTimer  started;
        int            attempts = 0;
    };

    void openDatabase();
    void createSchema();
    void resetInterrupted();      // error/missing-file rows go back to pending
    void startNext();             // begins the next pending track, if any
    void completeJob(const QString &tier, qint64 bytes, const QString &err);
    void scheduleNext(int elapsedMs, int durationSecs);
    void abortJob();              // stops the in-flight fetch, keeps row pending
    void cacheCoverArt(qint64 trackId);   // best-effort artwork pre-fetch
    QList<QString> playlistsHolding(qint64 trackId) const;
    void notifyPlaylistsHolding(qint64 trackId);

    TidalClient *m_client;
    QSqlDatabase m_db;
    QString      m_mediaDir;
    Job         *m_job = nullptr;     // one download at a time, by design
    bool         m_waiting = false;   // in the pacing gap between tracks
    int          m_paceOverrideMs = -1;
};
