#pragma once
#include <QObject>
#include "api/TidalApi.h"
#include "api/Auth.h"
#include "api/TidalClient.h"
#include "api/TidalBridge.h"
#include "player/Player.h"
#include "player/Downloader.h"
#include "library/LocalLibrary.h"
#include "mpris/MprisPlayer.h"
#include "ui/ImageProvider.h"
#include <QSystemTrayIcon>

class QQmlApplicationEngine;
class CastManager;
class MacNowPlaying;

class Application : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool reallyQuit READ reallyQuit NOTIFY reallyQuitChanged)
public:
    explicit Application(QObject *parent = nullptr);
    int run(int argc, char **argv);

    bool reallyQuit() const { return m_reallyQuit; }
    Q_INVOKABLE void quit();

    // ── Last viewed page ────────────────────────────────────────────────
    // Main.qml records the page it navigates to, and asks for it back on
    // start-up, so a restart returns to the playlist, album or search that
    // was open. The params are the JSON of the navigate() argument, which is
    // where the playlist uuid or album id lives. Stored per user: the ids
    // mean nothing to another account.
    // The previous page is stored with it, so the back button still works on
    // the restored page. Main.qml keeps one step of history, not a stack, so
    // one step is all there is to save.
    Q_INVOKABLE void    saveNavState(const QString &page,     const QString &paramsJson,
                                     const QString &prevPage, const QString &prevParamsJson);
    Q_INVOKABLE QString lastNavPage()       const;
    Q_INVOKABLE QString lastNavParams()     const;
    Q_INVOKABLE QString lastNavPrevPage()   const;
    Q_INVOKABLE QString lastNavPrevParams() const;

    void showWindow();
    void hideWindow();
    void toggleWindow();

signals:
    void reallyQuitChanged();

private:
    QString readNav(const QString &field) const;

    TidalApi    *m_api    = nullptr;
    Auth        *m_auth   = nullptr;
    TidalClient *m_client = nullptr;
    TidalBridge *m_bridge = nullptr;
    Player      *m_player = nullptr;
    Downloader  *m_downloader = nullptr;
    LocalLibrary*m_library = nullptr;
    CastManager *m_cast   = nullptr;
    MprisManager*m_mpris  = nullptr;
    MacNowPlaying *m_nowPlaying = nullptr;   // macOS media keys / Now Playing
    QSystemTrayIcon *m_trayIcon = nullptr;
    QQmlApplicationEngine *m_engine = nullptr;

    bool         m_reallyQuit = false;
};
