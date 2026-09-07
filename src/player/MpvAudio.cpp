#include "MpvAudio.h"

#include <QDebug>
#include <QGuiApplication>
#include <QTimer>
#include <QUuid>

#include <mpv/client.h>

#include <clocale>

#include "DashStream.h"
#include "PlaybackDiagnostics.h"

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

QString stateName(MpvAudio::State state) {
    switch (state) {
    case MpvAudio::State::Stopped: return QStringLiteral("stopped");
    case MpvAudio::State::Playing: return QStringLiteral("playing");
    case MpvAudio::State::Paused:  return QStringLiteral("paused");
    }
    return QStringLiteral("unknown");
}

QString endReasonName(mpv_end_file_reason reason) {
    switch (reason) {
    case MPV_END_FILE_REASON_EOF:      return QStringLiteral("eof");
    case MPV_END_FILE_REASON_STOP:     return QStringLiteral("stop");
    case MPV_END_FILE_REASON_QUIT:     return QStringLiteral("quit");
    case MPV_END_FILE_REASON_ERROR:    return QStringLiteral("error");
    case MPV_END_FILE_REASON_REDIRECT: return QStringLiteral("redirect");
    }
    return QStringLiteral("unknown");
}

bool keepMpvLogPrefix(const QString &prefix) {
    return prefix == QStringLiteral("ao")
        || prefix.startsWith(QStringLiteral("ao/"))
        || prefix == QStringLiteral("cplayer")
        || prefix == QStringLiteral("cache")
        || prefix.startsWith(QStringLiteral("cache/"))
        || prefix == QStringLiteral("demux")
        || prefix.startsWith(QStringLiteral("demux/"))
        || prefix == QStringLiteral("ffmpeg")
        || prefix.startsWith(QStringLiteral("ffmpeg/"))
        || prefix == QStringLiteral("stream")
        || prefix.startsWith(QStringLiteral("stream/"));
}

constexpr quint64 kQueryRequestBase = 0x5457100000000000ULL;

} // namespace

MpvAudio::MpvAudio(QObject *parent) : QObject(parent) {
    m_clock.start();
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    recordDiagnostic(QStringLiteral("backend-create"));
    // libmpv refuses to start unless LC_NUMERIC is "C" — it parses numbers with
    // the C locale and mpv_create() fails outright otherwise. Qt sets the
    // locale from the environment when QApplication is constructed, so this has
    // to be re-asserted here. Only the numeric category is forced, so date and
    // string formatting elsewhere in the app keep the user's locale.
    std::setlocale(LC_NUMERIC, "C");

    m_mpv = mpv_create();
    if (!m_mpv) {
        qWarning() << "[mpv] mpv_create failed — no audio backend";
        recordDiagnostic(QStringLiteral("backend-create-failed"));
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
        recordDiagnostic(QStringLiteral("backend-initialize-failed"), {
            {QStringLiteral("error"), QString::fromUtf8(mpv_error_string(rc))}
        });
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    // Lets Player hand mpv a tidalstream:// URL that pulls DASH segments on
    // demand instead of a fully downloaded file.
    DashStream::install(m_mpv);

    // Verbose mpv messages are filtered in drainEvents before they reach disk.
    // AO details are otherwise lost when a CoreAudio call wedges.
    mpv_request_log_messages(m_mpv, "v");

    const auto observe = [this](const char *name, mpv_format format) {
        const int result = mpv_observe_property(m_mpv, 0, name, format);
        if (result < 0) {
            recordDiagnostic(QStringLiteral("observe-property-failed"), {
                {QStringLiteral("property"), QString::fromUtf8(name)},
                {QStringLiteral("error"), QString::fromUtf8(mpv_error_string(result))}
            });
        }
    };
    observe("pause",                       MPV_FORMAT_FLAG);
    observe("idle-active",                 MPV_FORMAT_FLAG);
    observe("paused-for-cache",            MPV_FORMAT_FLAG);
    observe("core-idle",                   MPV_FORMAT_FLAG);
    observe("eof-reached",                 MPV_FORMAT_FLAG);
    observe("current-ao",                  MPV_FORMAT_STRING);
    observe("audio-device",                MPV_FORMAT_STRING);
    observe("cache-buffering-state",       MPV_FORMAT_INT64);
    observe("audio-out-params/samplerate", MPV_FORMAT_INT64);
    observe("audio-out-params/channels",   MPV_FORMAT_STRING);
    observe("audio-out-params/format",     MPV_FORMAT_STRING);
    mpv_set_wakeup_callback(m_mpv, wakeup, this);
    const unsigned long apiVersion = mpv_client_api_version();
    recordDiagnostic(QStringLiteral("backend-ready"), {
        {QStringLiteral("mpvClientApiVersion"),
         QStringLiteral("%1.%2").arg(apiVersion >> 16).arg(apiVersion & 0xffff)}
    });

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
    recordDiagnostic(QStringLiteral("backend-destroy"));
    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
    mpv_terminate_destroy(m_mpv);
    m_mpv = nullptr;
}

void MpvAudio::recordDiagnostic(const QString &event, QVariantMap fields) const {
    fields.insert(QStringLiteral("session"), m_sessionId);
    fields.insert(QStringLiteral("loadSerial"), static_cast<qulonglong>(m_loadSerial));
    PlaybackDiagnostics::record(event, fields);
}

QVariantMap MpvAudio::diagnosticSnapshot() const {
    const qint64 now = m_clock.isValid() ? m_clock.elapsed() : -1;
    QVariantMap snapshot {
        {QStringLiteral("state"), stateName(m_state)},
        {QStringLiteral("mediaStatus"), m_mediaStatus},
        {QStringLiteral("sourceScheme"), m_sourceScheme},
        {QStringLiteral("fileLoaded"), m_fileLoaded},
        {QStringLiteral("playbackRestarted"), m_playbackRestarted},
        {QStringLiteral("idleActive"), m_idle},
        {QStringLiteral("paused"), m_paused},
        {QStringLiteral("pausedForCache"), m_pausedForCache},
        {QStringLiteral("coreIdle"), m_coreIdle},
        {QStringLiteral("eofReached"), m_eofReached},
        {QStringLiteral("currentAo"), m_currentAo},
        {QStringLiteral("audioDevice"), m_audioDevice},
        {QStringLiteral("cacheBufferingState"), m_cacheBufferingState},
        {QStringLiteral("audioSampleRate"), m_audioSampleRate},
        {QStringLiteral("audioChannels"), m_audioChannels},
        {QStringLiteral("audioFormat"), m_audioFormat},
        {QStringLiteral("positionMs"), m_lastPosition},
        {QStringLiteral("durationMs"), m_lastDuration},
        {QStringLiteral("positionQueryPending"), m_positionRequest != 0},
        {QStringLiteral("durationQueryPending"), m_durationRequest != 0},
        {QStringLiteral("muted"), m_muted},
        {QStringLiteral("volume"), m_volume},
        {QStringLiteral("audioReloadCount"), static_cast<qulonglong>(m_audioReloadCount)},
        {QStringLiteral("recoveryAttempted"), m_recoveryAttempted}
    };
    snapshot.insert(QStringLiteral("sourceAgeMs"),
                    m_sourceRequestedAtMs >= 0 && now >= 0 ? now - m_sourceRequestedAtMs : -1);
    snapshot.insert(QStringLiteral("positionUnchangedMs"),
                    m_lastProgressAtMs >= 0 && now >= 0 ? now - m_lastProgressAtMs : -1);
    return snapshot;
}

void MpvAudio::captureDiagnostics(const QString &reason) const {
    QVariantMap fields = diagnosticSnapshot();
    fields.insert(QStringLiteral("reason"), reason);
    recordDiagnostic(QStringLiteral("snapshot"), fields);
}

bool MpvAudio::reloadAudioOutput(const QString &reason) {
    if (!m_mpv) return false;
    if (m_recoveryAttempted) {
        recordDiagnostic(QStringLiteral("audio-output-reload-skipped"), {
            {QStringLiteral("reason"), reason},
            {QStringLiteral("cause"), QStringLiteral("already-attempted")}
        });
        return false;
    }

    m_recoveryAttempted = true;
    ++m_audioReloadCount;
    // ao-reload is intentionally asynchronous. A synchronous command can pin
    // the Qt thread if the CoreAudio callback is already stuck.
    const char *cmd[] = {"ao-reload", nullptr};
    m_pendingReloadRequest = 0x5457000000000000ULL | m_audioReloadCount;
    const int rc = mpv_command_async(m_mpv, m_pendingReloadRequest, cmd);
    recordDiagnostic(QStringLiteral("audio-output-reload-requested"), {
        {QStringLiteral("reason"), reason},
        {QStringLiteral("request"), static_cast<qulonglong>(m_pendingReloadRequest)},
        {QStringLiteral("accepted"), rc >= 0},
        {QStringLiteral("error"), rc < 0 ? QString::fromUtf8(mpv_error_string(rc)) : QString()}
    });
    if (rc < 0) m_pendingReloadRequest = 0;
    return rc >= 0;
}

void MpvAudio::setSource(const QUrl &url) {
    if (!m_mpv) return;
    if (url.isEmpty()) {
        m_sourceScheme.clear();
        m_sourceRequestedAtMs = -1;
        m_fileLoaded = false;
        m_playbackRestarted = false;
        m_stallReported = false;
        m_recoveryAttempted = false;
        m_positionRequest = 0;
        m_durationRequest = 0;
        m_lastPosition = -1;
        m_lastDuration = -1;
        recordDiagnostic(QStringLiteral("source-cleared"));
        const char *cmd[] = {"stop", nullptr};
        mpv_command_async(m_mpv, 0, cmd);
        return;
    }
    ++m_loadSerial;
    m_sourceScheme = url.isLocalFile() ? QStringLiteral("file") : url.scheme();
    m_sourceRequestedAtMs = m_clock.elapsed();
    m_lastProgressAtMs = m_sourceRequestedAtMs;
    m_fileLoaded = false;
    m_playbackRestarted = false;
    m_eofReached = false;
    m_stallReported = false;
    m_recoveryAttempted = false;
    m_positionRequest = 0;
    m_durationRequest = 0;
    m_lastPosition = -1;
    m_lastDuration = -1;
    recordDiagnostic(QStringLiteral("source-requested"), {
        {QStringLiteral("scheme"), m_sourceScheme},
        {QStringLiteral("local"), url.isLocalFile()}
    });
    const QByteArray path = url.isLocalFile() ? url.toLocalFile().toUtf8()
                                              : url.toString().toUtf8();
    const char *cmd[] = {"loadfile", path.constData(), nullptr};
    const int rc = mpv_command_async(m_mpv, 0, cmd);
    if (rc < 0) {
        recordDiagnostic(QStringLiteral("source-command-failed"), {
            {QStringLiteral("error"), QString::fromUtf8(mpv_error_string(rc))}
        });
        emit errorOccurred(QString::fromUtf8(mpv_error_string(rc)));
    }
}

void MpvAudio::play() {
    if (!m_mpv) return;
    recordDiagnostic(QStringLiteral("play-requested"));
    int flag = 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvAudio::pause() {
    if (!m_mpv) return;
    recordDiagnostic(QStringLiteral("pause-requested"));
    int flag = 1;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvAudio::stop() {
    if (!m_mpv) return;
    recordDiagnostic(QStringLiteral("stop-requested"));
    const char *cmd[] = {"stop", nullptr};
    mpv_command_async(m_mpv, 0, cmd);
}

qint64 MpvAudio::position() const {
    if (!m_mpv || m_idle) return 0;
    return qMax(0LL, m_lastPosition);
}

qint64 MpvAudio::duration() const {
    if (!m_mpv || m_idle) return 0;
    return qMax(0LL, m_lastDuration);
}

void MpvAudio::setPosition(qint64 ms) {
    if (!m_mpv) return;
    recordDiagnostic(QStringLiteral("seek-requested"), {
        {QStringLiteral("positionMs"), ms}
    });
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
    if (s == State::Playing)
        m_lastProgressAtMs = m_clock.elapsed();
    if (m_poll) {
        if (s == State::Playing) m_poll->start();
        else                     m_poll->stop();
    }
    recordDiagnostic(QStringLiteral("state-changed"), {
        {QStringLiteral("state"), stateName(s)}
    });
    emit playbackStateChanged(s);
}

void MpvAudio::updateState() {
    if (m_idle)        setState(State::Stopped);
    else if (m_paused) setState(State::Paused);
    else               setState(State::Playing);
}

void MpvAudio::pollPosition() {
    if (!m_mpv || m_idle) return;

    // Queries stay asynchronous so a wedged mpv core cannot pin the Qt thread.
    // At most one request for each property is outstanding.
    if (m_positionRequest == 0) {
        m_positionRequest = kQueryRequestBase | ++m_queryCounter;
        if (mpv_get_property_async(m_mpv, m_positionRequest,
                                   "time-pos", MPV_FORMAT_DOUBLE) < 0)
            m_positionRequest = 0;
    }
    if (m_durationRequest == 0) {
        m_durationRequest = kQueryRequestBase | ++m_queryCounter;
        if (mpv_get_property_async(m_mpv, m_durationRequest,
                                   "duration", MPV_FORMAT_DOUBLE) < 0)
            m_durationRequest = 0;
    }

    const qint64 unchangedMs = m_lastProgressAtMs >= 0
        ? m_clock.elapsed() - m_lastProgressAtMs : 0;
    if (m_state == State::Playing && !m_pausedForCache && !m_stallReported
        && unchangedMs >= 10'000) {
        m_stallReported = true;
        captureDiagnostics(QStringLiteral("position-frozen-10s"));
        emit playbackStalled();
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
            m_mediaStatus = QStringLiteral("loading");
            recordDiagnostic(QStringLiteral("start-file"));
            emit mediaStatusChanged(Status::Loading);
            break;

        case MPV_EVENT_FILE_LOADED:
            m_idle = false;
            m_fileLoaded = true;
            m_mediaStatus = QStringLiteral("loaded");
            m_lastDuration = -1;          // force a durationChanged on next poll
            recordDiagnostic(QStringLiteral("file-loaded"));
            emit mediaStatusChanged(Status::Loaded);
            updateState();
            break;

        case MPV_EVENT_PLAYBACK_RESTART:
            // Fired once the decoder has audio ready, after a load or a seek.
            m_playbackRestarted = true;
            m_mediaStatus = QStringLiteral("buffered");
            m_lastProgressAtMs = m_clock.elapsed();
            recordDiagnostic(QStringLiteral("playback-restart"));
            emit mediaStatusChanged(Status::Buffered);
            updateState();
            // The tick above is stopped while paused, so this is what moves the
            // seek bar when the track is scrubbed without playing it.
            pollPosition();
            break;

        case MPV_EVENT_END_FILE: {
            auto *ef = static_cast<mpv_event_end_file *>(ev->data);
            m_idle = true;
            m_fileLoaded = false;
            m_positionRequest = 0;
            m_durationRequest = 0;
            m_lastPosition = -1;
            QVariantMap fields;
            if (ef) {
                fields.insert(QStringLiteral("reason"), endReasonName(ef->reason));
                fields.insert(QStringLiteral("error"), ef->error < 0
                    ? QString::fromUtf8(mpv_error_string(ef->error)) : QString());
            }
            recordDiagnostic(QStringLiteral("end-file"), fields);
            if (ef && ef->reason == MPV_END_FILE_REASON_EOF) {
                m_mediaStatus = QStringLiteral("end-of-media");
                emit mediaStatusChanged(Status::EndOfMedia);
            } else if (ef && ef->reason == MPV_END_FILE_REASON_ERROR) {
                m_mediaStatus = QStringLiteral("invalid-media");
                emit mediaStatusChanged(Status::InvalidMedia);
                emit errorOccurred(QString::fromUtf8(mpv_error_string(ef->error)));
            } else {
                m_mediaStatus = QStringLiteral("stopped");
            }
            // A STOP reason is our own stop()/setSource change — stay quiet, or
            // Player would treat every track change as a failure.
            updateState();
            break;
        }

        case MPV_EVENT_PROPERTY_CHANGE: {
            auto *prop = static_cast<mpv_event_property *>(ev->data);
            if (!prop || !prop->name || !prop->data) break;

            const QString name = QString::fromUtf8(prop->name);
            QVariant value;
            bool changed = false;
            bool affectsState = false;

            if (prop->format == MPV_FORMAT_FLAG) {
                const bool on = *static_cast<int *>(prop->data) != 0;
                value = on;
                if (name == QStringLiteral("pause")) {
                    changed = m_paused != on;
                    m_paused = on;
                    affectsState = true;
                } else if (name == QStringLiteral("idle-active")) {
                    changed = m_idle != on;
                    m_idle = on;
                    affectsState = true;
                } else if (name == QStringLiteral("paused-for-cache")) {
                    changed = m_pausedForCache != on;
                    if (m_pausedForCache && !on) m_lastProgressAtMs = m_clock.elapsed();
                    m_pausedForCache = on;
                } else if (name == QStringLiteral("core-idle")) {
                    changed = m_coreIdle != on;
                    m_coreIdle = on;
                } else if (name == QStringLiteral("eof-reached")) {
                    changed = m_eofReached != on;
                    m_eofReached = on;
                }
            } else if (prop->format == MPV_FORMAT_INT64) {
                const qint64 number = *static_cast<int64_t *>(prop->data);
                value = number;
                if (name == QStringLiteral("cache-buffering-state")) {
                    changed = m_cacheBufferingState != number;
                    m_cacheBufferingState = number;
                } else if (name == QStringLiteral("audio-out-params/samplerate")) {
                    changed = m_audioSampleRate != number;
                    m_audioSampleRate = number;
                }
            } else if (prop->format == MPV_FORMAT_STRING) {
                const char *text = *static_cast<char **>(prop->data);
                const QString string = text ? QString::fromUtf8(text) : QString();
                value = string;
                if (name == QStringLiteral("current-ao")) {
                    changed = m_currentAo != string;
                    m_currentAo = string;
                } else if (name == QStringLiteral("audio-device")) {
                    changed = m_audioDevice != string;
                    m_audioDevice = string;
                } else if (name == QStringLiteral("audio-out-params/channels")) {
                    changed = m_audioChannels != string;
                    m_audioChannels = string;
                } else if (name == QStringLiteral("audio-out-params/format")) {
                    changed = m_audioFormat != string;
                    m_audioFormat = string;
                }
            }

            if (changed) {
                recordDiagnostic(QStringLiteral("property-changed"), {
                    {QStringLiteral("property"), name},
                    {QStringLiteral("value"), value}
                });
            }
            if (affectsState) updateState();
            break;
        }

        case MPV_EVENT_LOG_MESSAGE: {
            auto *message = static_cast<mpv_event_log_message *>(ev->data);
            if (!message || !message->prefix || !message->text) break;
            const QString prefix = QString::fromUtf8(message->prefix);
            if (!keepMpvLogPrefix(prefix)) break;
            QString text = QString::fromUtf8(message->text).trimmed();
            if (text.size() > 2000) text.truncate(2000);
            recordDiagnostic(QStringLiteral("mpv-log"), {
                {QStringLiteral("prefix"), prefix},
                {QStringLiteral("level"), message->level
                    ? QString::fromUtf8(message->level) : QString()},
                {QStringLiteral("text"), text}
            });
            break;
        }

        case MPV_EVENT_COMMAND_REPLY:
            if (m_pendingReloadRequest != 0
                && ev->reply_userdata == m_pendingReloadRequest) {
                recordDiagnostic(QStringLiteral("audio-output-reload-completed"), {
                    {QStringLiteral("request"), static_cast<qulonglong>(ev->reply_userdata)},
                    {QStringLiteral("success"), ev->error >= 0},
                    {QStringLiteral("error"), ev->error < 0
                        ? QString::fromUtf8(mpv_error_string(ev->error)) : QString()}
                });
                m_pendingReloadRequest = 0;
            }
            break;

        case MPV_EVENT_GET_PROPERTY_REPLY: {
            auto *prop = static_cast<mpv_event_property *>(ev->data);
            if (ev->reply_userdata == m_positionRequest) {
                m_positionRequest = 0;
                if (ev->error < 0 || !prop || !prop->data
                    || prop->format != MPV_FORMAT_DOUBLE) break;

                const qint64 positionMs = static_cast<qint64>(
                    *static_cast<double *>(prop->data) * 1000.0);
                if (positionMs == m_lastPosition) break;

                const bool progressed = positionMs > m_lastPosition && positionMs > 0;
                const bool recovered = progressed && m_recoveryAttempted;
                m_lastPosition = positionMs;
                m_lastProgressAtMs = m_clock.elapsed();
                if (progressed) {
                    m_stallReported = false;
                    m_recoveryAttempted = false;
                    if (recovered) {
                        recordDiagnostic(QStringLiteral("position-progress-resumed"), {
                            {QStringLiteral("positionMs"), positionMs}
                        });
                    }
                }
                emit positionChanged(positionMs);
            } else if (ev->reply_userdata == m_durationRequest) {
                m_durationRequest = 0;
                if (ev->error < 0 || !prop || !prop->data
                    || prop->format != MPV_FORMAT_DOUBLE) break;

                const qint64 durationMs = static_cast<qint64>(
                    *static_cast<double *>(prop->data) * 1000.0);
                if (durationMs != m_lastDuration) {
                    m_lastDuration = durationMs;
                    emit durationChanged(durationMs);
                }
            }
            break;
        }

        case MPV_EVENT_SHUTDOWN:
            recordDiagnostic(QStringLiteral("mpv-shutdown"));
            break;

        default:
            break;
        }
    }
}
