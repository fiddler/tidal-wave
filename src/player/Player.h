#pragma once
#include <QObject>
#include "MpvAudio.h"
#include <QVariantMap>
#include <QVariantList>
#include <QTemporaryFile>
#include "api/TidalClient.h"
#include "api/Models.h"
#include <memory>

class QNetworkReply;
class CastSession;
class DashFetcher;
class DashStream;
class OfflineManager;

class Player : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool       playing      READ playing      NOTIFY playingChanged)
    Q_PROPERTY(bool       loading      READ loading      NOTIFY loadingChanged)
    Q_PROPERTY(qint64     position     READ position     NOTIFY positionChanged)
    Q_PROPERTY(qint64     duration     READ duration     NOTIFY durationChanged)
    Q_PROPERTY(double     volume       READ volume  WRITE setVolume  NOTIFY volumeChanged)
    Q_PROPERTY(bool       muted        READ muted   WRITE setMuted   NOTIFY mutedChanged)
    Q_PROPERTY(QVariantMap currentTrack READ currentTrackMap NOTIFY currentTrackChanged)
    Q_PROPERTY(bool       shuffle      READ shuffle WRITE setShuffle  NOTIFY shuffleChanged)
    Q_PROPERTY(int        repeatMode   READ repeatMode WRITE setRepeatMode NOTIFY repeatModeChanged)
    Q_PROPERTY(QString    audioQuality READ audioQuality NOTIFY currentTrackChanged)
    Q_PROPERTY(int        queueCount      READ queueCount      NOTIFY queueChanged)
    Q_PROPERTY(int        queueIndex      READ queueIndex      NOTIFY queueChanged)
    Q_PROPERTY(QVariantList queueTracks   READ queueTracks     NOTIFY queueChanged)
    Q_PROPERTY(QVariantList recentlyPlayed READ recentlyPlayed NOTIFY recentlyPlayedChanged)
    // "Playing from" context — where the current queue was started from.
    Q_PROPERTY(QString sourceType READ sourceType NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceId   READ sourceId   NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceChanged)

public:
    explicit Player(TidalClient *client, QObject *parent = nullptr);
    ~Player() override;

    bool        playing()     const;
    bool        loading()     const { return m_loading; }
    qint64      position()    const;
    qint64      duration()    const;
    double      volume()      const;
    bool        muted()       const;
    QVariantMap currentTrackMap() const;
    bool        shuffle()     const { return m_shuffle; }
    int         repeatMode()  const { return m_repeatMode; }
    QString     audioQuality()const;
    // Maps a raw Tidal quality code (LOW/HIGH/LOSSLESS/HI_RES_LOSSLESS) to the
    // user-facing label. Single source of truth so every badge stays consistent.
    Q_INVOKABLE QString qualityLabel(const QString &code) const;
    int          queueCount()    const { return m_queue.count(); }
    int          queueIndex()    const { return m_index; }
    QVariantList queueTracks()   const;
    QVariantList recentlyPlayed() const;
    QString      sourceType() const { return m_sourceType; }
    QString      sourceId()   const { return m_sourceId; }
    QString      sourceName() const { return m_sourceName; }

    // Records where playback was started from (e.g. "playlist"/"album"/"mix"/
    // "collection"/"radio"/"artist"). Call immediately before playTracks() from
    // the originating page so Now Playing can link back to it.
    Q_INVOKABLE void setPlaybackSource(const QString &type, const QString &id, const QString &name);

    // For MPRIS (internal use)
    Track currentTrack() const { return m_currentTrack; }
    qlonglong currentTrackId() const { return m_currentTrack.id; }

    // Optional offline cache: when set, tracks with a cached file play from
    // disk instead of streaming. Wired up by Application.
    void setOfflineStore(OfflineManager *offline) { m_offline = offline; }

    // Audio filter chain for the mpv backend (Equalizer is the only caller).
    // Audio init is deferred, so the string is held and applied in initAudio()
    // when it arrives early — same pattern as m_pendingVolume.
    void setAudioFilter(const QString &af);

    // ── Session persistence ─────────────────────────────────────────────
    // The queue, the current track and the playback offset survive a restart.
    // A restored session stays paused and holds no stream: the audio is only
    // fetched when the user presses play, which then starts at the saved
    // offset. Called on quit and periodically during playback.
    void saveSession() const;

    // ── Chromecast handoff ──────────────────────────────────────────────
    // While casting, playback lives on the device: transport routes to the
    // CastSession and position/duration/playing mirror the device's status.
    bool casting() const { return m_castSession != nullptr; }
    void beginCast(CastSession *session);
    void endCast();
    void onCastPosition(double sec);
    void onCastDuration(double sec);
    void onCastPlaying(bool playing);

    // QML-callable play methods — tracks are QVariantMaps from TidalBridge
    Q_INVOKABLE void playTracks      (const QVariantList &tracks, int startIndex = 0);
    Q_INVOKABLE void appendQueue     (const QVariantList &tracks);
    Q_INVOKABLE void jumpToQueue     (int index);
    Q_INVOKABLE void clearQueue      ();
    Q_INVOKABLE void removeFromQueue (int index);
    Q_INVOKABLE void moveQueueItem   (int from, int to);

    Q_INVOKABLE void playPause ();
    Q_INVOKABLE void next      ();
    Q_INVOKABLE void previous  ();
    Q_INVOKABLE void seek      (qint64 ms);
    Q_INVOKABLE void setVolume (double v);
    Q_INVOKABLE void setMuted  (bool m);
    Q_INVOKABLE void setShuffle    (bool s);
    Q_INVOKABLE void setRepeatMode (int  m);

    Q_INVOKABLE QVariantMap queueTrackAt(int index) const;
    // Tracks that will play after the current one, in true playback order
    // (respects shuffle). max < 0 means "all". Single source of truth for
    // every "up next" view so they stay consistent with what actually plays.
    Q_INVOKABLE QVariantList upcomingTracks(int max = -1) const;
    // The whole queue in true playback order. When shuffle is on, each entry
    // carries a "_queueIndex" with its real index in m_queue so the Queue panel
    // can still map rows back for jump/remove. Linear order when not shuffled.
    Q_INVOKABLE QVariantList playbackOrderTracks() const;

signals:
    void playingChanged     (bool playing);
    void loadingChanged     (bool loading);
    void positionChanged    (qint64 ms);
    void durationChanged    (qint64 ms);
    void volumeChanged      (double v);
    void mutedChanged       (bool m);
    void currentTrackChanged();
    void shuffleChanged      (bool s);
    void repeatModeChanged   (int  m);
    void queueChanged        ();
    void recentlyPlayedChanged();
    void sourceChanged       ();
    void castTrackChanged    ();   // current track changed while casting
    void error               (const QString &msg);

private slots:
    void initAudio();
    void onMediaStatusChanged(MpvAudio::Status status);
    void onPlaybackStateChanged(MpvAudio::State state);
    void onErrorOccurred(const QString &msg);

private:
    void handleUserIdChanged(qint64 uid);
    void restoreSession(qint64 uid);
    // startMs > 0 resumes a restored session: the offset is applied once the
    // media is loaded, not before, or QMediaPlayer drops the seek.
    void loadAndPlay(int index, qint64 startMs = 0);
    void setLoading(bool l);
    Track trackFromMap(const QVariantMap &m) const;
    void buildShuffleOrder();
    int  nextIndex() const;
    int  previousIndex() const;
    void preloadNext();
    void cancelPreload();

    TidalClient         *m_client;
    OfflineManager      *m_offline = nullptr;
    // Volume and mute live on the backend now; QAudioOutput is gone with
    // QMediaPlayer. m_pendingVolume/m_pendingMuted still hold the values until
    // initAudio() runs, exactly as before.
    MpvAudio            *m_player    = nullptr;
    double               m_pendingVolume = 0.7;
    bool                 m_pendingMuted  = false;
    QString              m_audioFilter;

    QList<QVariantMap>   m_queue;
    QList<QVariantMap>   m_recentlyPlayed;
    QList<int>           m_shuffleOrder;
    int                  m_index         = -1;
    Track                m_currentTrack;
    bool                 m_loading       = false;
    // Nothing outside this class clears m_loading; every path that ends a load
    // has to call setLoading(false) itself. A stream fetch that simply never
    // answers used to leave it true for good, and the track row's spinner is
    // an infinite animation — one stuck row kept the whole window rendering at
    // the display refresh rate until the app was quit.
    QTimer              *m_loadWatchdog  = nullptr;
    bool                 m_shuffle       = false;
    int                  m_repeatMode    = 0;  // 0=no 1=all 2=one
    QString              m_sourceType;
    QString              m_sourceId;
    QString              m_sourceName;
    // Set by setPlaybackSource(), consumed by the next playTracks(). Lets
    // playTracks() clear a stale "playing from" when a play has no source.
    bool                 m_pendingSource = false;
    QString              m_streamedQuality;

    // Session restore. m_sessionRestored means "the queue is back but nothing
    // was handed to QMediaPlayer yet"; m_pendingSeekMs is the offset that
    // position() reports until the media is loaded and the seek is applied.
    bool                 m_sessionRestored   = false;
    qint64               m_pendingSeekMs     = 0;
    qint64               m_lastSavedPosition = 0;

    // Cast state (non-null while casting; owned by CastManager).
    CastSession         *m_castSession   = nullptr;
    qint64               m_castPosition  = 0;   // ms
    qint64               m_castDuration  = 0;   // ms
    bool                 m_castPlaying   = false;
    QTemporaryFile      *m_mpdTempFile    = nullptr;
    QNetworkReply       *m_activeDownload = nullptr;
    // The DASH stream mpv is reading segments from. Retired on every track
    // change so a blocked reader is released.
    std::shared_ptr<DashStream> m_activeStream;

    // Preload state for the next queued track
    int                  m_preloadIndex    = -1;
    bool                 m_preloadReady    = false;
    QString              m_preloadQuality;
    QTemporaryFile      *m_preloadTempFile = nullptr;
    QNetworkReply       *m_preloadDownload = nullptr;
    DashFetcher         *m_preloadDash     = nullptr;
};
