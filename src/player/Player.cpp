#include "Player.h"
#include <QDebug>
#include <QUrl>
#include <QTimer>
#include <QNetworkReply>
#include <QSettings>
#include <QDir>
#include "cast/CastSession.h"
#include "library/OfflineManager.h"
#include "DashFetcher.h"
#include "MpvAudio.h"
#include "DashStream.h"
#include <algorithm>
#include <numeric>
#include <QRandomGenerator>

Player::Player(TidalClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{
    connect(m_client, &TidalClient::userIdChanged, this, &Player::handleUserIdChanged);
    if (m_client->userId() > 0) {
        handleUserIdChanged(m_client->userId());
    }

    // Defer audio device init until after the event loop starts to avoid
    // a PipeWire pw_thread_loop_lock deadlock under -O3 optimisation.
    QTimer::singleShot(0, this, &Player::initAudio);
}

void Player::initAudio() {
    m_player = new MpvAudio(this);
    if (!m_player->isValid()) {
        emit error(tr("Could not start the audio backend (libmpv)."));
        return;
    }
    m_player->setVolume(m_pendingVolume);
    m_player->setMuted(m_pendingMuted);
    if (!m_audioFilter.isEmpty())
        m_player->setAudioFilter(m_audioFilter);

    connect(m_player, &MpvAudio::mediaStatusChanged,
            this, &Player::onMediaStatusChanged);
    connect(m_player, &MpvAudio::playbackStateChanged,
            this, &Player::onPlaybackStateChanged);
    connect(m_player, &MpvAudio::errorOccurred,
            this, &Player::onErrorOccurred);
    connect(m_player, &MpvAudio::playbackStalled, this, [this]() {
        // MpvAudio wrote a snapshot before emitting this. Reopen only mpv's
        // output; never restart coreaudiod automatically because that destroys
        // the system-side evidence we need for the incident bundle.
        if (m_player)
            m_player->reloadAudioOutput(QStringLiteral("position-frozen-10s"));
    });
    connect(m_player, &MpvAudio::positionChanged, this, [this](qint64 pos) {
        qint64 dur = m_player->duration();
        if (dur > 10000 && pos > 0 && (dur - pos) <= 10000)
            preloadNext();
        // Keep the saved offset close to reality without writing on every tick.
        // A crash or a force quit then loses at most ten seconds of playback.
        if (qAbs(pos - m_lastSavedPosition) > 10000) {
            m_lastSavedPosition = pos;
            saveSession();
        }
        emit positionChanged(pos);
    });
    connect(m_player, &MpvAudio::durationChanged,
            this, &Player::durationChanged);
}

void Player::setAudioFilter(const QString &af) {
    m_audioFilter = af;
    if (m_player) m_player->setAudioFilter(af);
}

Player::~Player() {
    cancelPreload();
    delete m_mpdTempFile;
}

bool   Player::playing()  const { return casting() ? m_castPlaying  : (m_player && m_player->playbackState() == MpvAudio::State::Playing); }

qint64 Player::position() const {
    if (casting()) return m_castPosition;
    // A restored session, and a track that is still loading, report the offset
    // they will start at. Without this the seek bar and the player bar drop to
    // 0:00 against a track that is plainly not at its start.
    if (m_pendingSeekMs > 0) return m_pendingSeekMs;
    return m_player ? m_player->position() : 0;
}

qint64 Player::duration() const {
    if (casting()) return m_castDuration;
    const qint64 d = m_player ? m_player->duration() : 0;
    if (d > 0) return d;
    // Before the media loads, the queue entry carries the length in seconds.
    return currentTrackMap().value(QStringLiteral("duration")).toLongLong() * 1000LL;
}
double Player::volume()   const { return m_player ? m_player->volume()  : m_pendingVolume; }
bool   Player::muted()    const { return m_player ? m_player->isMuted() : m_pendingMuted; }

QVariantMap Player::currentTrackMap() const {
    if (m_index < 0 || m_index >= m_queue.count()) return {};
    return m_queue[m_index];
}

void Player::setLoading(bool l) {
    if (!m_loadWatchdog) {
        m_loadWatchdog = new QTimer(this);
        m_loadWatchdog->setSingleShot(true);
        m_loadWatchdog->setInterval(30'000);
        connect(m_loadWatchdog, &QTimer::timeout, this, [this]() {
            qWarning() << "[play] still loading after 30s; capturing diagnostics"
                          " and reloading the audio output";
            if (m_player) {
                m_player->captureDiagnostics(QStringLiteral("loading-30s"));
                m_player->reloadAudioOutput(QStringLiteral("loading-30s"));
            }
            setLoading(false);
        });
    }
    // Armed before the early return below: picking a second track while the
    // first is still loading calls setLoading(true) again without changing the
    // flag, and that load deserves its own 30 seconds rather than inheriting
    // whatever is left of the previous one.
    if (l) m_loadWatchdog->start();
    else   m_loadWatchdog->stop();
    if (m_loading == l) return;
    m_loading = l;
    emit loadingChanged(l);
}

// ─── QML-callable ──────────────────────────────────

void Player::playTracks(const QVariantList &tracks, int startIndex) {
    if (tracks.isEmpty()) return;
    // Clear a stale "playing from" if this play didn't set its own source.
    if (!m_pendingSource && !m_sourceType.isEmpty()) {
        m_sourceType.clear();
        m_sourceId.clear();
        m_sourceName.clear();
        emit sourceChanged();
    }
    m_pendingSource = false;
    cancelPreload();
    m_queue.clear();
    for (const auto &v : tracks)
        m_queue.append(v.toMap());
    m_index = qBound(0, startIndex, m_queue.count() - 1);
    if (m_shuffle) buildShuffleOrder();
    emit queueChanged();
    loadAndPlay(m_index);
}

void Player::setPlaybackSource(const QString &type, const QString &id, const QString &name) {
    m_pendingSource = true;   // consumed by the playTracks() that follows
    if (m_sourceType == type && m_sourceId == id && m_sourceName == name) return;
    m_sourceType = type;
    m_sourceId   = id;
    m_sourceName = name;
    emit sourceChanged();
}

void Player::appendQueue(const QVariantList &tracks) {
    int insertAt = (m_index >= 0) ? m_index + 1 : m_queue.count();
    for (int i = 0; i < tracks.count(); i++) {
        QVariantMap t = tracks[i].toMap();
        t["_userQueued"] = true;
        m_queue.insert(insertAt + i, t);
    }
    if (m_shuffle) buildShuffleOrder();
    emit queueChanged();
}

void Player::jumpToQueue(int index) {
    if (index < 0 || index >= m_queue.count()) return;
    cancelPreload();
    m_index = index;
    emit queueChanged();
    loadAndPlay(m_index);
}

void Player::clearQueue() {
    cancelPreload();
    if (m_player) m_player->stop();
    m_queue.clear();
    m_shuffleOrder.clear();
    m_index = -1;
    m_currentTrack = Track{};
    m_sessionRestored = false;
    m_pendingSeekMs   = 0;
    setLoading(false);
    saveSession();          // an empty queue clears the stored session
    emit currentTrackChanged();
    emit queueChanged();
}

void Player::removeFromQueue(int index) {
    if (index < 0 || index >= m_queue.count()) return;
    m_queue.removeAt(index);
    if (index < m_index) {
        m_index--;
    } else if (index == m_index) {
        if (m_queue.isEmpty()) {
            if (m_player) m_player->stop();
            m_index = -1;
            m_currentTrack = Track{};
            setLoading(false);
            emit currentTrackChanged();
        } else {
            m_index = qMin(m_index, m_queue.count() - 1);
            loadAndPlay(m_index);
        }
    }
    if (m_shuffle) buildShuffleOrder();
    emit queueChanged();
}

void Player::moveQueueItem(int from, int to) {
    if (from < 0 || from >= m_queue.count() ||
        to   < 0 || to   >= m_queue.count() || from == to) return;
    m_queue.move(from, to);
    if      (m_index == from)                          m_index = to;
    else if (from < m_index && to >= m_index)          m_index--;
    else if (from > m_index && to <= m_index)          m_index++;
    if (m_shuffle) buildShuffleOrder();
    emit queueChanged();
}

QVariantMap Player::queueTrackAt(int index) const {
    if (index < 0 || index >= m_queue.count()) return {};
    return m_queue[index];
}

QVariantList Player::queueTracks() const {
    QVariantList out;
    for (const auto &m : m_queue)
        out.append(m);
    return out;
}

QVariantList Player::upcomingTracks(int max) const {
    QVariantList out;
    if (m_queue.isEmpty() || m_index < 0) return out;
    if (m_shuffle) {
        // Walk the shuffle permutation forward from the current track.
        int si = m_shuffleOrder.indexOf(m_index);
        for (int i = si + 1; i >= 0 && i < m_shuffleOrder.count(); ++i) {
            out.append(m_queue[m_shuffleOrder[i]]);
            if (max >= 0 && out.count() >= max) break;
        }
    } else {
        for (int i = m_index + 1; i < m_queue.count(); ++i) {
            out.append(m_queue[i]);
            if (max >= 0 && out.count() >= max) break;
        }
    }
    return out;
}

QVariantList Player::playbackOrderTracks() const {
    if (!m_shuffle) return queueTracks();
    QVariantList out;
    for (int idx : m_shuffleOrder) {
        if (idx < 0 || idx >= m_queue.count()) continue;
        QVariantMap m = m_queue[idx];
        m[QStringLiteral("_queueIndex")] = idx;
        out.append(m);
    }
    return out;
}

QVariantList Player::recentlyPlayed() const {
    QVariantList out;
    for (const auto &m : m_recentlyPlayed)
        out.append(m);
    return out;
}

void Player::playPause() {
#ifdef Q_OS_LINUX
    if (casting()) {
        if (m_castPlaying) m_castSession->pause();
        else               m_castSession->play();
        return;
    }
#endif
    if (!m_player) return;
    // A restored session holds a queue but no stream. Asking QMediaPlayer to
    // play an empty source would do nothing, so fetch the track and start it
    // at the offset the last run ended on.
    if (m_sessionRestored) {
        loadAndPlay(m_index, m_pendingSeekMs);
        return;
    }
    if (m_player->playbackState() == MpvAudio::State::Playing)
        m_player->pause();
    else
        m_player->play();
}

void Player::next() {
    if (!m_player) return;
    int n = nextIndex();
    if (n < 0) { m_player->stop(); return; }
    m_index = n;
    emit queueChanged();
    loadAndPlay(m_index);
}

void Player::previous() {
    if (position() > 3000) { seek(0); return; }
    int p = previousIndex();
    if (p < 0) { seek(0); return; }
    m_index = p;
    emit queueChanged();
    loadAndPlay(m_index);
}

void Player::seek(qint64 ms) {
#ifdef Q_OS_LINUX
    if (casting()) {
        if (m_castSession) m_castSession->seek(ms / 1000.0);
        m_castPosition = ms;              // optimistic; device status corrects it
        emit positionChanged(ms);
        return;
    }
#endif
    // Nothing is loaded yet in a restored session, so there is nothing to seek.
    // Remember the offset instead: play then starts there. This keeps the seek
    // bar usable before the first play of a restored track.
    if (m_sessionRestored) {
        m_pendingSeekMs = qBound(0LL, ms, duration());
        emit positionChanged(m_pendingSeekMs);
        return;
    }
    if (m_player) m_player->setPosition(ms);
}

void Player::setVolume(double v) {
    m_pendingVolume = qBound(0.0, v, 1.0);
#ifdef Q_OS_LINUX
    if (casting() && m_castSession) m_castSession->setVolume(m_pendingVolume);
#endif
    if (m_player) m_player->setVolume(m_pendingVolume);
    emit volumeChanged(m_pendingVolume);
}

void Player::setMuted(bool m) {
    m_pendingMuted = m;
    // While casting the local output is force-muted; don't override that here
    // (the preference is re-applied on endCast). Mute still updates the UI state.
    if (m_player && !casting()) m_player->setMuted(m);
    emit mutedChanged(m);
}

void Player::setShuffle(bool s) {
    m_shuffle = s;
    if (s) buildShuffleOrder();
    emit shuffleChanged(s);
    // The upcoming-tracks order depends on shuffle, so refresh every queue view.
    emit queueChanged();
}

void Player::setRepeatMode(int m) {
    m_repeatMode = m;
    emit repeatModeChanged(m);
}

// ─── Internals ─────────────────────────────────────

int Player::nextIndex() const {
    if (m_repeatMode == 2) return m_index;   // repeat one
    if (m_shuffle) {
        int si = m_shuffleOrder.indexOf(m_index);
        if (si < m_shuffleOrder.count() - 1) return m_shuffleOrder[si + 1];
        if (m_repeatMode == 1) return m_shuffleOrder[0];
        return -1;
    }
    if (m_index < m_queue.count() - 1) return m_index + 1;
    if (m_repeatMode == 1) return 0;
    return -1;
}

int Player::previousIndex() const {
    if (m_shuffle) {
        int si = m_shuffleOrder.indexOf(m_index);
        if (si > 0) return m_shuffleOrder[si - 1];
        return -1;
    }
    if (m_index > 0) return m_index - 1;
    return -1;
}

void Player::buildShuffleOrder() {
    m_shuffleOrder.resize(m_queue.count());
    std::iota(m_shuffleOrder.begin(), m_shuffleOrder.end(), 0);
    for (int i = m_shuffleOrder.count() - 1; i > 0; --i) {
        int j = QRandomGenerator::global()->bounded(i + 1);
        std::swap(m_shuffleOrder[i], m_shuffleOrder[j]);
    }
    if (m_index >= 0) {
        int pos = m_shuffleOrder.indexOf(m_index);
        if (pos > 0) std::swap(m_shuffleOrder[0], m_shuffleOrder[pos]);
    }
}

Track Player::trackFromMap(const QVariantMap &m) const {
    Track t;
    t.id        = m["id"].toLongLong();
    t.title     = m["title"].toString();
    t.duration  = m["duration"].toInt();
    t.localPath = m["localPath"].toString();
    t.album.title = m["albumTitle"].toString();
    t.album.id    = m["albumId"].toLongLong();
    t.album.cover = m["albumCover"].toString();
    // Parse artists string back to artist struct (simplified)
    Artist a;
    a.name = m["artists"].toString();
    a.id   = m["artistId"].toLongLong();
    t.artists.append(a);
    return t;
}

void Player::loadAndPlay(int index, qint64 startMs) {
    if (!m_player || index < 0 || index >= m_queue.count()) return;

    // From here the queue is no longer only restored state: a stream is being
    // fetched. startMs is applied in onMediaStatusChanged once the media is
    // ready, and reported by position() until then.
    m_sessionRestored   = false;
    m_pendingSeekMs     = qMax(0LL, startMs);
    m_lastSavedPosition = m_pendingSeekMs;

    // Tidal keeps retired tracks in playlists but refuses to stream them. The
    // UI greys them out, so this only fires on "Play all", shuffle, or an
    // auto-advance: step past them instead of stalling on a stream error.
    // The counter bounds the walk when every remaining track is unavailable.
    if (!m_queue[index].value(QStringLiteral("available"), true).toBool()) {
        for (int guard = m_queue.count(); guard > 0; --guard) {
            m_index = index;
            int n = nextIndex();
            if (n < 0 || n == index) break;
            index = n;
            if (m_queue[index].value(QStringLiteral("available"), true).toBool()) break;
        }
        if (!m_queue[index].value(QStringLiteral("available"), true).toBool()) {
            m_player->stop();
            setLoading(false);
            emit error(tr("This track is no longer available on Tidal."));
            return;
        }
        m_index = index;
        emit queueChanged();
    }

    // Order matters: stop() and clearing the source make QMediaPlayer report
    // LoadedMedia, which onMediaStatusChanged turns back into loading=false.
    // Raising the flag before them meant the row became the current track with
    // neither the loading nor the playing state, so next/previous showed
    // nothing at all until the audio started.
    m_player->stop();
    m_player->setSource(QUrl());
    setLoading(true);

    if (m_activeDownload) {
        auto *dl = m_activeDownload;
        m_activeDownload = nullptr;
        dl->abort();
        dl->deleteLater();
    }

    if (m_activeStream) {
        DashStream::retire(m_activeStream);
        m_activeStream.reset();
    }

    if (m_mpdTempFile) {
        m_mpdTempFile->remove();
        delete m_mpdTempFile;
        m_mpdTempFile = nullptr;
    }

    m_streamedQuality.clear();
    m_currentTrack = trackFromMap(m_queue[index]);
    emit currentTrackChanged();

    // Track recently played (max 20 unique entries)
    QVariantMap trackMap = m_queue[index];
    qlonglong trackId = trackMap.value("id").toLongLong();
    for (int i = m_recentlyPlayed.count() - 1; i >= 0; --i) {
        if (m_recentlyPlayed.at(i).value("id").toLongLong() == trackId)
            m_recentlyPlayed.removeAt(i);
    }
    m_recentlyPlayed.prepend(trackMap);
    if (m_recentlyPlayed.count() > 20)
        m_recentlyPlayed = m_recentlyPlayed.mid(0, 20);
    emit recentlyPlayedChanged();

    // Save recently played to settings
    qint64 uid = m_client->userId();
    if (uid > 0) {
        QVariantList saveList;
        for (const auto &m : m_recentlyPlayed) {
            saveList.append(m);
        }
        QSettings settings;
        settings.setValue(QStringLiteral("user_%1/playback/recentlyPlayed").arg(uid), saveList);
    }

    // Every track change is a new offset of zero, so persist it at once
    // instead of waiting for the periodic save.
    saveSession();

    // Local library file — there is no manifest to fetch and nothing to
    // preload, so hand the path straight to the player.
    if (m_currentTrack.isLocal()) {
        cancelPreload();
        if (casting()) {
            setLoading(false);
            emit error(tr("Local files cannot be cast to a remote device."));
            return;
        }
        m_streamedQuality = m_queue[index].value("quality").toString();
        // Loading stays true until the media actually plays — see
        // onMediaStatusChanged/onPlaybackStateChanged. Clearing it here would
        // leave the row with neither the loading nor the playing state during
        // the gap between setSource() and the first audio.
        m_player->setSource(QUrl::fromLocalFile(m_currentTrack.localPath));
        m_player->play();
        return;
    }

    // If casting, don't play locally — hand the (already-updated) current track
    // off to the cast device; CastManager re-prepares and LOADs it.
    if (casting()) {
        setLoading(false);
        emit castTrackChanged();
        return;
    }

    // Offline cache hit — play the pinned copy from disk, no network at all.
    if (m_offline) {
        const QString cached = m_offline->localPathFor(m_currentTrack.id);
        if (!cached.isEmpty()) {
            cancelPreload();
            m_streamedQuality = m_offline->tierFor(m_currentTrack.id);
            emit currentTrackChanged();
            qInfo() << "[play] track" << m_currentTrack.id << "from offline cache";
            m_player->setSource(QUrl::fromLocalFile(cached));
            m_player->play();
            return;
        }
    }

    // Use preloaded file if it's ready for this exact index
    if (m_preloadIndex == index && m_preloadReady && m_preloadTempFile) {
        m_mpdTempFile     = m_preloadTempFile;
        m_preloadTempFile = nullptr;
        m_streamedQuality = m_preloadQuality;
        m_preloadIndex    = -1;
        m_preloadReady    = false;
        m_preloadQuality  = {};
        emit currentTrackChanged();
        // Keep loading true here too. This is the path next/previous normally
        // takes — the track is already preloaded — so clearing it made the new
        // row show nothing at all until the audio started, while a double click
        // (no preload) correctly showed the spinner.
        m_player->setSource(QUrl::fromLocalFile(m_mpdTempFile->fileName()));
        m_player->play();
        return;
    }

    // Preload is for a different track or still in progress — discard it
    cancelPreload();

    qlonglong loadingTrackId = m_currentTrack.id;
    m_client->fetchStreamManifest(loadingTrackId,
        [this, loadingTrackId](StreamManifest manifest, QString err) {
            if (m_currentTrack.id != loadingTrackId) {
                return;
            }
            if (!err.isEmpty()) {
                setLoading(false);
                emit error("Stream error: " + err);
                return;
            }
            m_streamedQuality = manifest.codec;
            emit currentTrackChanged();
            qInfo() << "[play] track" << loadingTrackId << "quality" << manifest.codec
                    << "manifest" << (manifest.type == StreamManifest::BTS ? "BTS" : "DASH");

            if (manifest.type == StreamManifest::BTS) {
                m_activeDownload = m_client->fetchRaw(QUrl(manifest.url), [this, loadingTrackId](QByteArray data, QString err) {
                    m_activeDownload = nullptr;
                    if (m_currentTrack.id != loadingTrackId) {
                        return;
                    }
                    if (!err.isEmpty() || data.isEmpty()) {
                        setLoading(false);
                        emit error("Failed to download audio stream: " + err);
                        return;
                    }
                    m_mpdTempFile = new QTemporaryFile(
                        QDir::tempPath() + QStringLiteral("/tidal-wave-XXXXXX.mp4"));
                    m_mpdTempFile->setAutoRemove(false);
                    if (m_mpdTempFile->open()) {
                        m_mpdTempFile->write(data);
                        m_mpdTempFile->flush();
                        m_mpdTempFile->close();
                        // Casting may have started while this fetch was in flight;
                        // if so, hand off to the device instead of playing locally.
                        if (casting()) { setLoading(false); emit castTrackChanged(); return; }
                        m_player->setSource(QUrl::fromLocalFile(m_mpdTempFile->fileName()));
                        m_player->play();
                    } else {
                        setLoading(false);
                        emit error("Failed to write temporary audio file");
                    }
                });
            } else {
                // DASH (lossless and hi-res). mpv pulls segments through the
                // tidalstream protocol as it needs them, so playback starts
                // after roughly one segment rather than the whole ~30 MB track.
                auto stream = DashStream::create(manifest.url);
                if (!stream) {
                    setLoading(false);
                    emit error(tr("Could not read the lossless stream manifest."));
                    return;
                }
                m_activeStream = stream;
                if (casting()) { setLoading(false); emit castTrackChanged(); return; }
                m_player->setSource(stream->url());
                m_player->play();
            }
        });
}

void Player::onMediaStatusChanged(MpvAudio::Status status) {
    switch (status) {
    case MpvAudio::Status::Loading:
        setLoading(true); break;
    case MpvAudio::Status::Buffered:
    case MpvAudio::Status::Loaded:
        // The media is ready, so a restored offset can finally be applied.
        // Clear it first: position() must report the player from now on.
        if (m_pendingSeekMs > 0) {
            const qint64 target = m_pendingSeekMs;
            m_pendingSeekMs = 0;
            m_player->setPosition(target);
        }
        setLoading(false); break;
    case MpvAudio::Status::EndOfMedia:
        setLoading(false);
        next();
        break;
    case MpvAudio::Status::InvalidMedia:
        setLoading(false);
        emit error("Invalid media");
        break;
    default: break;
    }
}

void Player::onPlaybackStateChanged(MpvAudio::State state) {
    // The first audio always ends the loading state, whatever the media status
    // reported. This is the backstop that keeps a row from holding the spinner
    // after it has started to play.
    if (state == MpvAudio::State::Playing)
        setLoading(false);
    emit playingChanged(state == MpvAudio::State::Playing);
}

void Player::onErrorOccurred(const QString &msg) {
    setLoading(false);
    if (m_player) m_player->captureDiagnostics(QStringLiteral("player-error"));
    qWarning() << "Player error:" << msg;
    emit error(msg);
}

void Player::cancelPreload() {
    if (m_preloadDownload) {
        auto *dl = m_preloadDownload;
        m_preloadDownload = nullptr;
        dl->abort();
        dl->deleteLater();
    }
    if (m_preloadDash) {
        auto *df = m_preloadDash;
        m_preloadDash = nullptr;
        df->abort();
        df->deleteLater();
    }
    if (m_preloadTempFile) {
        m_preloadTempFile->remove();
        delete m_preloadTempFile;
        m_preloadTempFile = nullptr;
    }
    m_preloadIndex   = -1;
    m_preloadReady   = false;
    m_preloadQuality = {};
}

void Player::preloadNext() {
    int next = nextIndex();
    if (next < 0 || next == m_preloadIndex) return;

    cancelPreload();

    // Local files open instantly and have no manifest — nothing to preload.
    if (!m_queue[next].value("localPath").toString().isEmpty()) return;

    // Same for offline-cached tracks: they open straight from disk.
    if (m_offline && !m_offline->localPathFor(m_queue[next].value("id").toLongLong()).isEmpty())
        return;

    m_preloadIndex = next;

    qlonglong trackId = m_queue[next].value("id").toLongLong();

    m_client->fetchStreamManifest(trackId, [this, next](StreamManifest manifest, QString err) {
        if (m_preloadIndex != next || !err.isEmpty()) return;

        m_preloadQuality = manifest.codec;

        if (manifest.type == StreamManifest::BTS) {
            m_preloadDownload = m_client->fetchRaw(QUrl(manifest.url), [this, next](QByteArray data, QString dlErr) {
                m_preloadDownload = nullptr;
                if (m_preloadIndex != next || !dlErr.isEmpty() || data.isEmpty()) return;
                auto *f = new QTemporaryFile(QDir::tempPath() + QStringLiteral("/tidal-wave-XXXXXX.mp4"));
                f->setAutoRemove(false);
                if (f->open()) {
                    f->write(data); f->flush(); f->close();
                    m_preloadTempFile = f;
                    m_preloadReady    = true;
                } else {
                    delete f;
                }
            });
        } else {
            auto *fetcher = new DashFetcher(m_client, manifest.url, this);
            if (!fetcher->isValid()) {
                delete fetcher;
                m_preloadIndex = -1;
                return;
            }
            m_preloadDash = fetcher;
            connect(fetcher, &DashFetcher::finished, this,
                [this, fetcher, next](QTemporaryFile *file, const QString &) {
                    if (m_preloadDash == fetcher) m_preloadDash = nullptr;
                    fetcher->deleteLater();
                    if (m_preloadIndex != next || !file) {
                        if (file) { file->remove(); delete file; }
                        return;
                    }
                    m_preloadTempFile = file;
                    m_preloadReady    = true;
                });
            fetcher->start();
        }
    });
}

QString Player::audioQuality() const {
    if (m_streamedQuality.isEmpty()) {
        return QString();
    }

    AudioQuality pref = m_client->audioQuality();

    AudioQuality maxQuality = AudioQuality::Lossless;
    if (m_streamedQuality == QStringLiteral("LOW")) maxQuality = AudioQuality::Low96k;
    else if (m_streamedQuality == QStringLiteral("HIGH")) maxQuality = AudioQuality::Low320k;
    else if (m_streamedQuality == QStringLiteral("LOSSLESS")) maxQuality = AudioQuality::Lossless;
    else if (m_streamedQuality == QStringLiteral("HI_RES_LOSSLESS")) maxQuality = AudioQuality::HiResLossless;

    AudioQuality actual = pref;
    if (static_cast<int>(maxQuality) < static_cast<int>(pref)) {
        actual = maxQuality;
    }

    switch (actual) {
        case AudioQuality::Low96k:        return QStringLiteral("LOW");
        case AudioQuality::Low320k:       return QStringLiteral("HIGH");
        case AudioQuality::Lossless:      return QStringLiteral("LOSSLESS");
        case AudioQuality::HiResLossless: return QStringLiteral("HI_RES_LOSSLESS");
    }
    return QStringLiteral("LOSSLESS");
}

QString Player::qualityLabel(const QString &code) const {
    // Tidal-consistent tier names (matches the labels Tidal's own apps show),
    // replacing the older "HI-FI"/"MASTER" branding.
    if (code == QStringLiteral("HI_RES_LOSSLESS")) return QStringLiteral("Max");
    if (code == QStringLiteral("LOSSLESS"))        return QStringLiteral("Lossless");
    if (code == QStringLiteral("HIGH"))            return QStringLiteral("High");
    if (code == QStringLiteral("LOW"))             return QStringLiteral("Low");
    return code;
}

void Player::beginCast(CastSession *session) {
    m_castSession = session;
    // Silence local output; playback continues on the device. Muting the output
    // (not just pausing) is a hard guard: any in-flight stream fetch that resolves
    // after this point must not leak audio to the local speakers alongside the cast.
    if (m_player) m_player->pause();
    if (m_player) m_player->setMuted(true);
    cancelPreload();
    m_castPosition = 0;
    m_castDuration = duration();   // seed from current until the device reports
    m_castPlaying  = true;
    emit playingChanged(true);
}

void Player::endCast() {
    if (!m_castSession) return;
    m_castSession = nullptr;
    m_castPosition = 0;
    m_castPlaying  = false;
    // Restore the user's local mute preference (beginCast force-muted the output).
    if (m_player) m_player->setMuted(m_pendingMuted);
    // Resume playback locally from the top of the current track.
    if (m_index >= 0 && m_index < m_queue.count())
        loadAndPlay(m_index);
    else
        emit playingChanged(false);
}

void Player::onCastPosition(double sec) {
    m_castPosition = qint64(sec * 1000.0);
    if (casting()) emit positionChanged(m_castPosition);
}

void Player::onCastDuration(double sec) {
    const qint64 d = qint64(sec * 1000.0);
    if (d <= 0) return;
    m_castDuration = d;
    if (casting()) emit durationChanged(m_castDuration);
}

void Player::onCastPlaying(bool playing) {
    if (m_castPlaying == playing) return;
    m_castPlaying = playing;
    if (casting()) emit playingChanged(playing);
}

void Player::handleUserIdChanged(qint64 uid) {
    m_recentlyPlayed.clear();
    if (uid > 0) {
        QSettings settings;
        QVariantList list = settings.value(QStringLiteral("user_%1/playback/recentlyPlayed").arg(uid)).toList();
        for (const auto &v : list) {
            m_recentlyPlayed.append(v.toMap());
        }
    }
    emit recentlyPlayedChanged();
    restoreSession(uid);
}

// The key that holds the saved session for one user.
static QString sessionKey(qint64 uid) {
    return QStringLiteral("user_%1/playback/session").arg(uid);
}

// A long playlist would write a large blob to QSettings on every save, so the
// stored queue is bounded. The slice keeps the current track and what follows
// it, which is what a restart needs.
static constexpr int kMaxSavedQueue = 500;

void Player::saveSession() const {
    const qint64 uid = m_client ? m_client->userId() : 0;
    if (uid <= 0) return;

    QSettings settings;
    const QString base = sessionKey(uid);
    if (m_queue.isEmpty() || m_index < 0 || m_index >= m_queue.count()) {
        settings.remove(base);
        return;
    }

    int start = 0;
    int index = m_index;
    if (m_queue.count() > kMaxSavedQueue) {
        start = qBound(0, m_index - 100, m_queue.count() - kMaxSavedQueue);
        index = m_index - start;
    }
    QVariantList queue;
    const int end = qMin(m_queue.count(), start + kMaxSavedQueue);
    for (int i = start; i < end; ++i)
        queue.append(m_queue[i]);

    settings.setValue(base + QStringLiteral("/queue"),      queue);
    settings.setValue(base + QStringLiteral("/index"),      index);
    settings.setValue(base + QStringLiteral("/position"),   position());
    settings.setValue(base + QStringLiteral("/shuffle"),    m_shuffle);
    settings.setValue(base + QStringLiteral("/repeat"),     m_repeatMode);
    settings.setValue(base + QStringLiteral("/sourceType"), m_sourceType);
    settings.setValue(base + QStringLiteral("/sourceId"),   m_sourceId);
    settings.setValue(base + QStringLiteral("/sourceName"), m_sourceName);
}

// Puts the last session back without touching QMediaPlayer, so nothing plays
// and no stream is fetched. playPause() turns this into real playback.
void Player::restoreSession(qint64 uid) {
    // Never replace live playback — this also runs on a later login.
    if (uid <= 0 || !m_queue.isEmpty()) return;

    QSettings settings;
    const QString base = sessionKey(uid);
    const QVariantList queue = settings.value(base + QStringLiteral("/queue")).toList();
    if (queue.isEmpty()) return;

    for (const auto &v : queue)
        m_queue.append(v.toMap());
    m_index = qBound(0, settings.value(base + QStringLiteral("/index")).toInt(), m_queue.count() - 1);
    m_pendingSeekMs     = qMax(0LL, settings.value(base + QStringLiteral("/position")).toLongLong());
    m_lastSavedPosition = m_pendingSeekMs;
    m_repeatMode        = settings.value(base + QStringLiteral("/repeat")).toInt();
    m_shuffle           = settings.value(base + QStringLiteral("/shuffle")).toBool();
    // The exact shuffle permutation is not stored: it is random anyway, and
    // buildShuffleOrder() puts the restored track first.
    if (m_shuffle) buildShuffleOrder();
    m_sourceType   = settings.value(base + QStringLiteral("/sourceType")).toString();
    m_sourceId     = settings.value(base + QStringLiteral("/sourceId")).toString();
    m_sourceName   = settings.value(base + QStringLiteral("/sourceName")).toString();
    m_currentTrack = trackFromMap(m_queue[m_index]);
    m_sessionRestored = true;

    emit queueChanged();
    emit shuffleChanged(m_shuffle);
    emit repeatModeChanged(m_repeatMode);
    emit sourceChanged();
    emit currentTrackChanged();
    emit durationChanged(duration());
    emit positionChanged(m_pendingSeekMs);
}
