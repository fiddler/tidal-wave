#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

struct mpv_handle;
class QTimer;

// Audio backend built on libmpv, in place of QMediaPlayer.
//
// Why: Qt's macOS backend is AVFoundation, which plays only what AVFoundation
// understands and gives no useful error when it does not — a Tidal DASH
// manifest simply sat at 0:00. libmpv carries its own demuxers and decoders, so
// the same code path works on every platform, and it opens the door to feeding
// segments in through mpv_stream_cb_add_ro instead of downloading a whole track
// before the first note.
//
// The API deliberately mirrors the QMediaPlayer members Player already used,
// so the swap stayed mechanical and reviewable.
class MpvAudio : public QObject {
    Q_OBJECT
public:
    // Own enums rather than QMediaPlayer's. Reusing Qt's kept the original swap
    // mechanical, but it also kept Qt Multimedia — and the GStreamer stack it
    // drags in on Linux — linked for nothing but three enum types.
    enum class State  { Stopped, Playing, Paused };
    enum class Status { Loading, Loaded, Buffered, EndOfMedia, InvalidMedia };
    Q_ENUM(State)
    Q_ENUM(Status)

    explicit MpvAudio(QObject *parent = nullptr);
    ~MpvAudio() override;

    // True when libmpv started. Everything below is a safe no-op when false.
    bool isValid() const { return m_mpv != nullptr; }

    void setSource(const QUrl &url);
    void play();
    void pause();
    void stop();

    qint64 position() const;
    qint64 duration() const;
    void   setPosition(qint64 ms);

    State playbackState() const { return m_state; }

    // Volume is 0.0–1.0 here, as QAudioOutput had it; mpv works in 0–100.
    double volume() const { return m_volume; }
    void   setVolume(double v);
    bool   isMuted() const { return m_muted; }
    void   setMuted(bool m);

    // Replaces the whole audio filter chain (mpv "af" property). The string is
    // mpv af syntax, e.g. "@eq:lavfi=[equalizer=...]"; empty clears the chain.
    // mpv swaps filters mid-playback without a dropout, and the property
    // sticks across loadfile calls, so one set covers all future tracks.
    void   setAudioFilter(const QString &af);

    // Cached state only. These never make synchronous libmpv calls, which is
    // important when the mpv core or CoreAudio is the thing that is stuck.
    QVariantMap diagnosticSnapshot() const;
    void captureDiagnostics(const QString &reason) const;

    // Reopens mpv's audio output without restarting the app or coreaudiod.
    // Returns whether the command was accepted for asynchronous execution.
    bool reloadAudioOutput(const QString &reason);

signals:
    void mediaStatusChanged(Status status);
    void playbackStateChanged(State state);
    void errorOccurred(const QString &msg);
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);
    void playbackStalled();

private slots:
    // Drains every queued libmpv event. Invoked queued from mpv's own thread.
    void drainEvents();
    void pollPosition();

private:
    void setState(State s);
    void updateState();
    void recordDiagnostic(const QString &event, QVariantMap fields = {}) const;

    mpv_handle *m_mpv = nullptr;
    QTimer     *m_poll = nullptr;
    State m_state = State::Stopped;
    double m_volume = 0.7;
    bool   m_muted  = false;
    bool   m_idle   = true;
    bool   m_paused = false;
    bool   m_pausedForCache = false;
    bool   m_coreIdle = false;
    bool   m_eofReached = false;
    bool   m_fileLoaded = false;
    bool   m_playbackRestarted = false;
    bool   m_stallReported = false;
    bool   m_recoveryAttempted = false;
    qint64 m_cacheBufferingState = -1;
    qint64 m_audioSampleRate = -1;
    QString m_audioChannels;
    QString m_audioFormat;
    QString m_currentAo;
    QString m_audioDevice;
    QString m_sourceScheme;
    QString m_mediaStatus = QStringLiteral("none");
    QString m_sessionId;
    quint64 m_loadSerial = 0;
    quint64 m_audioReloadCount = 0;
    quint64 m_pendingReloadRequest = 0;
    quint64 m_queryCounter = 0;
    quint64 m_positionRequest = 0;
    quint64 m_durationRequest = 0;
    QElapsedTimer m_clock;
    qint64 m_sourceRequestedAtMs = -1;
    qint64 m_lastProgressAtMs = -1;
    qint64 m_lastPosition = -1;
    qint64 m_lastDuration = -1;
};
