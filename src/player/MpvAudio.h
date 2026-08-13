#pragma once
#include <QObject>
#include <QString>
#include <QUrl>

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

signals:
    void mediaStatusChanged(Status status);
    void playbackStateChanged(State state);
    void errorOccurred(const QString &msg);
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);

private slots:
    // Drains every queued libmpv event. Invoked queued from mpv's own thread.
    void drainEvents();
    void pollPosition();

private:
    void setState(State s);
    void updateState();
    double getDouble(const char *name) const;
    bool   getFlag(const char *name) const;

    mpv_handle *m_mpv = nullptr;
    QTimer     *m_poll = nullptr;
    State m_state = State::Stopped;
    double m_volume = 0.7;
    bool   m_muted  = false;
    bool   m_idle   = true;
    bool   m_paused = false;
    qint64 m_lastPosition = -1;
    qint64 m_lastDuration = -1;
};
