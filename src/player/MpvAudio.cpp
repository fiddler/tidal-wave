#include "MpvAudio.h"

#include <QDebug>
#include <QGuiApplication>
#include <QTimer>

#include <mpv/client.h>

#include <clocale>

#include "DashStream.h"

namespace {

// Seeded from the state the app is already in, not just from later changes: a
// launch straight into the background (a login item, a media key on a restored
// session) never emits applicationStateChanged, and would otherwise keep the
// fast tick for as long as the app stayed unfocused.
int pollIntervalFor(Qt::ApplicationState state) {
    return state == Qt::ApplicationActive ? 200 : 1000;
}

// Called from libmpv's own thread. Nothing here may touch Qt state directly —
// it only nudges the object to drain the queue on the Qt thread.
void wakeup(void *ctx) {
    QMetaObject::invokeMethod(static_cast<MpvAudio *>(ctx), "drainEvents",
                              Qt::QueuedConnection);
}

} // namespace

MpvAudio::MpvAudio(QObject *parent) : QObject(parent) {
    // libmpv refuses to start unless LC_NUMERIC is "C" — it parses numbers with
    // the C locale and mpv_create() fails outright otherwise. Qt sets the
    // locale from the environment when QApplication is constructed, so this has
    // to be re-asserted here. Only the numeric category is forced, so date and
    // string formatting elsewhere in the app keep the user's locale.
    std::setlocale(LC_NUMERIC, "C");

    m_mpv = mpv_create();
    if (!m_mpv) {
        qWarning() << "[mpv] mpv_create failed — no audio backend";
        return;
    }

    // Nothing here wants mpv's own UI layer. `config=no` keeps a stray
    // ~/.config/mpv/mpv.conf from redirecting the audio output behind our
    // back; the rest turn off the scripts mpv loads by default, each of which
    // costs a thread and a Lua VM in a process that never shows a video
    // window. `load-scripts` only covers the user's own scripts directory —
    // the built-ins have one switch each, and they are named per mpv version,
    // so an unknown one here is a shrug rather than a failure.
    for (const char *opt : {"config", "load-scripts", "load-commands",
                            "load-console", "load-context-menu",
                            "load-positioning", "load-select",
                            "load-stats-overlay", "load-auto-profiles",
                            "osc", "ytdl"}) {
        if (mpv_set_option_string(m_mpv, opt, "no") < 0)
            qWarning() << "[mpv] no such option:" << opt;
    }
    // Audio-only player: no window, no video decoding, no terminal output.
    mpv_set_option_string(m_mpv, "vid",           "no");
    mpv_set_option_string(m_mpv, "audio-display", "no");
    mpv_set_option_string(m_mpv, "terminal",      "no");
    mpv_set_option_string(m_mpv, "msg-level",     "all=error");
    // idle keeps the core alive between tracks; without it mpv shuts down at
    // the end of a file and the next loadfile has nothing to run on.
    mpv_set_option_string(m_mpv, "idle",          "yes");
    // Let end-of-file surface as an event instead of mpv holding the last frame.
    mpv_set_option_string(m_mpv, "keep-open",     "no");
    mpv_set_option_string(m_mpv, "gapless-audio", "yes");
    // The stream is fetched over the network, so buffer generously.
    mpv_set_option_string(m_mpv, "cache",         "yes");

    const int rc = mpv_initialize(m_mpv);
    if (rc < 0) {
        qWarning() << "[mpv] initialize failed:" << mpv_error_string(rc);
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    // Lets Player hand mpv a tidalstream:// URL that pulls DASH segments on
    // demand instead of a fully downloaded file.
    DashStream::install(m_mpv);

    mpv_observe_property(m_mpv, 0, "pause",       MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "idle-active", MPV_FORMAT_FLAG);
    mpv_set_wakeup_callback(m_mpv, wakeup, this);

    setVolume(m_volume);
    setMuted(m_muted);

    // mpv reports time-pos far more often than a seek bar needs. Polling on a
    // fixed tick keeps the UI update rate predictable instead of tying it to
    // the decoder. It runs only while the audio does: a paused or stopped
    // player has no position to report, and this used to keep waking the
    // process five times a second for the life of the app.
    m_poll = new QTimer(this);
    m_poll->setInterval(pollIntervalFor(qGuiApp->applicationState()));
    connect(m_poll, &QTimer::timeout, this, &MpvAudio::pollPosition);
    // Every tick repaints the window. While the app is in the background that
    // buys nothing: the elapsed time is only accurate to the second anyway, so
    // the only thing the extra four ticks render is a slightly smoother seek
    // bar that nobody is looking at.
    connect(qGuiApp, &QGuiApplication::applicationStateChanged,
            this, [this](Qt::ApplicationState state) {
        m_poll->setInterval(pollIntervalFor(state));
    });
}

MpvAudio::~MpvAudio() {
    if (!m_mpv) return;
    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
    mpv_terminate_destroy(m_mpv);
    m_mpv = nullptr;
}

double MpvAudio::getDouble(const char *name) const {
    if (!m_mpv) return 0.0;
    double v = 0.0;
    return mpv_get_property(m_mpv, name, MPV_FORMAT_DOUBLE, &v) < 0 ? 0.0 : v;
}

bool MpvAudio::getFlag(const char *name) const {
    if (!m_mpv) return false;
    int v = 0;
    return mpv_get_property(m_mpv, name, MPV_FORMAT_FLAG, &v) < 0 ? false : v != 0;
}

void MpvAudio::setSource(const QUrl &url) {
    if (!m_mpv) return;
    if (url.isEmpty()) {
        const char *cmd[] = {"stop", nullptr};
        mpv_command_async(m_mpv, 0, cmd);
        return;
    }
    const QByteArray path = url.isLocalFile() ? url.toLocalFile().toUtf8()
                                              : url.toString().toUtf8();
    const char *cmd[] = {"loadfile", path.constData(), nullptr};
    const int rc = mpv_command_async(m_mpv, 0, cmd);
    if (rc < 0)
        emit errorOccurred(QString::fromUtf8(mpv_error_string(rc)));
}

void MpvAudio::play() {
    if (!m_mpv) return;
    int flag = 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvAudio::pause() {
    if (!m_mpv) return;
    int flag = 1;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvAudio::stop() {
    if (!m_mpv) return;
    const char *cmd[] = {"stop", nullptr};
    mpv_command_async(m_mpv, 0, cmd);
}

qint64 MpvAudio::position() const {
    if (!m_mpv || m_idle) return 0;
    return static_cast<qint64>(getDouble("time-pos") * 1000.0);
}

qint64 MpvAudio::duration() const {
    if (!m_mpv || m_idle) return 0;
    return static_cast<qint64>(getDouble("duration") * 1000.0);
}

void MpvAudio::setPosition(qint64 ms) {
    if (!m_mpv) return;
    const QByteArray target = QByteArray::number(ms / 1000.0, 'f', 3);
    const char *cmd[] = {"seek", target.constData(), "absolute", nullptr};
    mpv_command_async(m_mpv, 0, cmd);
}

void MpvAudio::setVolume(double v) {
    m_volume = qBound(0.0, v, 1.0);
    if (!m_mpv) return;
    double pct = m_volume * 100.0;
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &pct);
}

void MpvAudio::setMuted(bool m) {
    m_muted = m;
    if (!m_mpv) return;
    int flag = m ? 1 : 0;
    mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &flag);
}

void MpvAudio::setAudioFilter(const QString &af) {
    if (!m_mpv) return;
    const int rc = mpv_set_property_string(m_mpv, "af", af.toUtf8().constData());
    // A rejected string leaves the previous chain in place — playback is never
    // at risk, so a warning is all this needs.
    if (rc < 0)
        qWarning() << "[mpv] set af failed:" << mpv_error_string(rc) << af;
}

void MpvAudio::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    if (m_poll) {
        if (s == State::Playing) m_poll->start();
        else                     m_poll->stop();
    }
    emit playbackStateChanged(s);
}

void MpvAudio::updateState() {
    if (m_idle)        setState(State::Stopped);
    else if (m_paused) setState(State::Paused);
    else               setState(State::Playing);
}

void MpvAudio::pollPosition() {
    if (!m_mpv || m_idle) return;

    const qint64 pos = position();
    if (pos != m_lastPosition) {
        m_lastPosition = pos;
        emit positionChanged(pos);
    }
    const qint64 dur = duration();
    if (dur != m_lastDuration) {
        m_lastDuration = dur;
        emit durationChanged(dur);
    }
}

void MpvAudio::drainEvents() {
    if (!m_mpv) return;

    for (;;) {
        mpv_event *ev = mpv_wait_event(m_mpv, 0);
        if (!ev || ev->event_id == MPV_EVENT_NONE) return;

        switch (ev->event_id) {
        case MPV_EVENT_START_FILE:
            m_idle = false;
            emit mediaStatusChanged(Status::Loading);
            break;

        case MPV_EVENT_FILE_LOADED:
            m_idle = false;
            m_lastDuration = -1;          // force a durationChanged on next poll
            emit mediaStatusChanged(Status::Loaded);
            updateState();
            break;

        case MPV_EVENT_PLAYBACK_RESTART:
            // Fired once the decoder has audio ready, after a load or a seek.
            emit mediaStatusChanged(Status::Buffered);
            updateState();
            // The tick above is stopped while paused, so this is what moves the
            // seek bar when the track is scrubbed without playing it.
            pollPosition();
            break;

        case MPV_EVENT_END_FILE: {
            auto *ef = static_cast<mpv_event_end_file *>(ev->data);
            m_idle = true;
            m_lastPosition = -1;
            if (ef && ef->reason == MPV_END_FILE_REASON_EOF) {
                emit mediaStatusChanged(Status::EndOfMedia);
            } else if (ef && ef->reason == MPV_END_FILE_REASON_ERROR) {
                emit mediaStatusChanged(Status::InvalidMedia);
                emit errorOccurred(QString::fromUtf8(mpv_error_string(ef->error)));
            }
            // A STOP reason is our own stop()/setSource change — stay quiet, or
            // Player would treat every track change as a failure.
            updateState();
            break;
        }

        case MPV_EVENT_PROPERTY_CHANGE: {
            auto *prop = static_cast<mpv_event_property *>(ev->data);
            if (!prop || prop->format != MPV_FORMAT_FLAG || !prop->data) break;
            const bool on = *static_cast<int *>(prop->data) != 0;
            if (qstrcmp(prop->name, "pause") == 0)            m_paused = on;
            else if (qstrcmp(prop->name, "idle-active") == 0) m_idle   = on;
            updateState();
            break;
        }

        default:
            break;
        }
    }
}
