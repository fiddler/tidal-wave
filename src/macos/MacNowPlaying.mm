#include "MacNowPlaying.h"
#include "player/Player.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QtMath>

#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>

namespace {
// The Now Playing dictionary is kept between updates so the artwork — which
// arrives later, over the network — survives a metadata refresh. Application
// creates exactly one MacNowPlaying, so a file-scope dictionary is enough.
NSMutableDictionary *g_info = nil;
}

MacNowPlaying::MacNowPlaying(Player *player, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_net(new QNetworkAccessManager(this))
{
    g_info = [NSMutableDictionary dictionary];

    registerCommands();

    connect(m_player, &Player::currentTrackChanged, this, &MacNowPlaying::publishMetadata);
    connect(m_player, &Player::playingChanged,  this, [this](bool)   { publishState(); });
    connect(m_player, &Player::durationChanged, this, [this](qint64) { publishState(); });
    connect(m_player, &Player::positionChanged, this, [this](qint64 ms) {
        // macOS extrapolates the elapsed time from the playback rate, so an
        // update on every tick is waste. Republish only when the position
        // jumps — a seek, or a new track that starts at zero.
        if (qAbs(ms - m_lastPosition) > 1500)
            publishState();
        m_lastPosition = ms;
    });

    publishMetadata();
}

MacNowPlaying::~MacNowPlaying() {
    MPRemoteCommandCenter *cc = [MPRemoteCommandCenter sharedCommandCenter];
    [cc.playCommand                   removeTarget:nil];
    [cc.pauseCommand                  removeTarget:nil];
    [cc.togglePlayPauseCommand        removeTarget:nil];
    [cc.nextTrackCommand              removeTarget:nil];
    [cc.previousTrackCommand          removeTarget:nil];
    [cc.changePlaybackPositionCommand removeTarget:nil];

    MPNowPlayingInfoCenter *center = [MPNowPlayingInfoCenter defaultCenter];
    center.playbackState = MPNowPlayingPlaybackStateStopped;
    center.nowPlayingInfo = nil;
    g_info = nil;
}

// Hands each remote command to the player. The handlers can run on any thread,
// so every call is queued onto the thread the player lives on.
void MacNowPlaying::registerCommands() {
    MPRemoteCommandCenter *cc = [MPRemoteCommandCenter sharedCommandCenter];
    Player *p = m_player;

    auto post = [p](std::function<void()> fn) {
        QMetaObject::invokeMethod(p, [fn]() { fn(); }, Qt::QueuedConnection);
    };

    cc.playCommand.enabled = YES;
    [cc.playCommand addTargetWithHandler:^(MPRemoteCommandEvent *) {
        post([p] { if (!p->playing()) p->playPause(); });
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    cc.pauseCommand.enabled = YES;
    [cc.pauseCommand addTargetWithHandler:^(MPRemoteCommandEvent *) {
        post([p] { if (p->playing()) p->playPause(); });
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    // The keyboard play/pause key sends this one.
    cc.togglePlayPauseCommand.enabled = YES;
    [cc.togglePlayPauseCommand addTargetWithHandler:^(MPRemoteCommandEvent *) {
        post([p] { p->playPause(); });
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    cc.nextTrackCommand.enabled = YES;
    [cc.nextTrackCommand addTargetWithHandler:^(MPRemoteCommandEvent *) {
        post([p] { p->next(); });
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    cc.previousTrackCommand.enabled = YES;
    [cc.previousTrackCommand addTargetWithHandler:^(MPRemoteCommandEvent *) {
        post([p] { p->previous(); });
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    // Scrubbing from the Control Centre / lock screen slider.
    cc.changePlaybackPositionCommand.enabled = YES;
    [cc.changePlaybackPositionCommand addTargetWithHandler:^(MPRemoteCommandEvent *event) {
        auto *e = (MPChangePlaybackPositionCommandEvent *)event;
        const qint64 ms = (qint64)(e.positionTime * 1000.0);
        post([p, ms] { p->seek(ms); });
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    // Commands the player does not implement. They must be switched off, or
    // macOS shows dead controls for them.
    cc.stopCommand.enabled          = NO;
    cc.seekForwardCommand.enabled   = NO;
    cc.seekBackwardCommand.enabled  = NO;
    cc.skipForwardCommand.enabled   = NO;
    cc.skipBackwardCommand.enabled  = NO;
    cc.ratingCommand.enabled        = NO;
    cc.likeCommand.enabled          = NO;
    cc.dislikeCommand.enabled       = NO;
    cc.bookmarkCommand.enabled      = NO;
}

void MacNowPlaying::publishMetadata() {
    const QVariantMap t = m_player->currentTrackMap();
    if (t.isEmpty()) {
        [g_info removeAllObjects];
        m_artUrl.clear();
        MPNowPlayingInfoCenter *center = [MPNowPlayingInfoCenter defaultCenter];
        center.nowPlayingInfo = nil;
        center.playbackState = MPNowPlayingPlaybackStateStopped;
        return;
    }

    g_info[MPMediaItemPropertyTitle]      = t.value("title").toString().toNSString();
    g_info[MPMediaItemPropertyArtist]     = t.value("artists").toString().toNSString();
    g_info[MPMediaItemPropertyAlbumTitle] = t.value("albumTitle").toString().toNSString();
    g_info[MPNowPlayingInfoPropertyMediaType] = @(MPNowPlayingInfoMediaTypeAudio);

    const int trackNumber = t.value("trackNumber").toInt();
    if (trackNumber > 0)
        g_info[MPMediaItemPropertyAlbumTrackNumber] = @(trackNumber);
    else
        [g_info removeObjectForKey:MPMediaItemPropertyAlbumTrackNumber];

    // The art of a new track must not stay on screen while the next cover
    // downloads, so drop the old one first.
    const QString cover = t.value("coverUrl").toString();
    if (cover != m_artUrl) {
        [g_info removeObjectForKey:MPMediaItemPropertyArtwork];
        m_artUrl.clear();
        fetchArtwork(cover);
    }

    m_lastPosition = m_player->position();
    publishState();
}

// Publishes duration, elapsed time and the play state. The play state is what
// makes this app the owner of the media keys, so it is set on every change.
void MacNowPlaying::publishState() {
    if (!g_info || g_info.count == 0) return;

    // duration() is only known once the stream is loaded; the queue entry
    // carries the length in seconds and covers the gap.
    double seconds = m_player->duration() / 1000.0;
    if (seconds <= 0)
        seconds = m_player->currentTrackMap().value("duration").toDouble();

    const bool playing = m_player->playing();
    g_info[MPMediaItemPropertyPlaybackDuration]             = @(seconds);
    g_info[MPNowPlayingInfoPropertyElapsedPlaybackTime]     = @(m_player->position() / 1000.0);
    g_info[MPNowPlayingInfoPropertyPlaybackRate]            = @(playing ? 1.0 : 0.0);
    g_info[MPNowPlayingInfoPropertyDefaultPlaybackRate]     = @(1.0);

    MPNowPlayingInfoCenter *center = [MPNowPlayingInfoCenter defaultCenter];
    center.nowPlayingInfo = g_info;
    center.playbackState = playing ? MPNowPlayingPlaybackStatePlaying
                                   : MPNowPlayingPlaybackStatePaused;
}

// Downloads the cover art and attaches it to the Now Playing entry. The URL is
// remote for Tidal tracks and file:// for the local library — QNetworkAccessManager
// reads both.
void MacNowPlaying::fetchArtwork(const QString &url) {
    if (m_artReply) {
        m_artReply->abort();
        m_artReply = nullptr;
    }
    if (url.isEmpty()) return;

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_net->get(req);
    m_artReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();
        if (m_artReply == reply)
            m_artReply = nullptr;
        if (reply->error() != QNetworkReply::NoError) return;
        // A newer track started while this cover downloaded.
        if (m_player->currentTrackMap().value("coverUrl").toString() != url) return;

        const QByteArray bytes = reply->readAll();
        NSData *data = [NSData dataWithBytes:bytes.constData() length:(NSUInteger)bytes.size()];
        NSImage *image = [[NSImage alloc] initWithData:data];
        if (!image) return;

        MPMediaItemArtwork *art =
            [[MPMediaItemArtwork alloc] initWithBoundsSize:image.size
                                            requestHandler:^NSImage *(CGSize) { return image; }];
        g_info[MPMediaItemPropertyArtwork] = art;
        m_artUrl = url;
        [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = g_info;
    });
}
