#pragma once
#include <QObject>
#include <QString>

class Player;
class QNetworkAccessManager;
class QNetworkReply;

// macOS "Now Playing" integration — the counterpart of MprisPlayer on Linux.
//
// Two Apple APIs do the work:
//   * MPRemoteCommandCenter takes the keyboard media keys (play/pause, next,
//     previous) and the Control Centre transport buttons. Without a registered
//     handler macOS gives the keys to Music.app, which is why they used to
//     launch it while this player was playing.
//   * MPNowPlayingInfoCenter publishes the track metadata and the play state
//     that Control Centre, the Touch Bar and the lock screen show. The play
//     state also decides which app currently owns the media keys, so it must
//     be kept in sync with the player.
//
// This header stays pure C++ so Application.cpp does not need Objective-C++.
class MacNowPlaying : public QObject {
    Q_OBJECT
public:
    explicit MacNowPlaying(Player *player, QObject *parent = nullptr);
    ~MacNowPlaying() override;

private:
    void registerCommands();
    void publishMetadata();
    void publishState();
    void fetchArtwork(const QString &url);

    Player                *m_player       = nullptr;
    QNetworkAccessManager *m_net          = nullptr;
    QNetworkReply         *m_artReply     = nullptr;
    QString                m_artUrl;       // URL of the artwork now published
    qint64                 m_lastPosition = 0;
};
