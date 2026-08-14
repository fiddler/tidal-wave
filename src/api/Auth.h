#pragma once
#include <QObject>
#include <QTimer>
#include "TidalApi.h"

class Auth : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY userCodeChanged)
    Q_PROPERTY(QString verificationUrl READ verificationUrl NOTIFY userCodeChanged)
    Q_PROPERTY(QString username READ username NOTIFY usernameChanged)
    // userId lands together with the login state flip, so stateChanged covers it.
    Q_PROPERTY(qint64 userId READ userId NOTIFY stateChanged)

public:
    // Appended, never reordered: QML compares state against the raw ints.
    enum class State { LoggedOut, PendingDevice, LoggedIn, PendingPkce };
    Q_ENUM(State)

    explicit Auth(TidalApi *api, QObject *parent = nullptr);

    State   state()           const { return m_state; }
    QString userCode()        const { return m_userCode; }
    QString verificationUrl() const { return m_verificationUri; }
    QString username()        const { return m_username; }
    QString accessToken()     const { return m_accessToken; }
    QString refreshToken()    const { return m_refreshToken; }
    qint64  userId()          const { return m_userId; }
    QString countryCode()     const { return m_countryCode; }

    // Attempt device flow login. Tidal caps this client at 320 kbps AAC, so it
    // is kept only as a fallback — prefer startPkceFlow().
    Q_INVOKABLE void startDeviceFlow();
    // PKCE login. This is the only flow Tidal serves LOSSLESS and
    // HI_RES_LOSSLESS to. Builds the authorize URL into verificationUrl; the
    // caller opens it, then hands the redirected "Oops" page URL back to
    // submitPkceRedirect().
    Q_INVOKABLE void startPkceFlow();
    Q_INVOKABLE void submitPkceRedirect(const QString &redirectUrl);
    // Cancel pending auth (either flow)
    Q_INVOKABLE void cancelDeviceFlow();
    // Log out
    Q_INVOKABLE void logout();

    // Called on startup with persisted tokens
    void loadCredentials();

signals:
    void stateChanged(State state);
    void userCodeChanged();
    void usernameChanged();
    void loginSucceeded();
    void loginFailed(const QString &reason);
    void sessionExpired();

private slots:
    void pollForToken();
    void refreshAccessToken();

private:
    void setState(State s);
    void fetchSession();
    void saveCredentials();
    void clearCredentials();

    TidalApi  *m_api;
    QTimer    *m_pollTimer;
    QTimer    *m_refreshTimer;
    State      m_state = State::LoggedOut;

    QString m_deviceCode;
    QString m_userCode;
    QString m_verificationUri;
    int     m_pollInterval = 5;

    // PKCE flow state. m_isPkce also decides which client credentials the
    // refresh grant must use, so it is persisted with the tokens.
    bool    m_isPkce = false;
    QString m_codeVerifier;
    QString m_clientUniqueKey;

    QString m_accessToken;
    QString m_refreshToken;
    QDateTime m_tokenExpiry;
    qint64  m_userId     = 0;
    QString m_countryCode;
    QString m_username;

    // Device-code client. Tidal serves it HIGH (320 kbps AAC) and nothing more,
    // whatever the account's subscription or the requested audioquality is.
    static constexpr auto kClientId     = "fX2JxdmntZWK0ixT";
    static constexpr auto kClientSecret = "1Nn9AfDAjxrgJFJbKNWLeAyKGVGmINuXPPLHVXAzxAg=";

    // PKCE client — the one entitled to LOSSLESS / HI_RES_LOSSLESS. Its secret
    // is kept in the same double-base64 form upstream (python-tidal) ships it
    // in, so the constant stays diffable when upstream rotates it. decodeCreds()
    // unwraps it; the id decodes to "6BDSRdpK9hqEBTgU".
    static constexpr auto kPkceIdA      = "TmtKRVUxSmtjRXM=";
    static constexpr auto kPkceIdB      = "NWFIRkZRbFJuVlE9PQ==";
    static constexpr auto kPkceSecretA  = "ZUdWMVVHMVpOMjVpY0ZvNVNVbGlURUZqVVQ=";
    static constexpr auto kPkceSecretB  = "a3pjMmhyWVRGV1RtaGxWVUZ4VGpaSlkzTjZhbFJIT0QwPQ==";
    static QString decodeCreds(const char *a, const char *b);
    static QString pkceClientId()     { return decodeCreds(kPkceIdA, kPkceIdB); }
    static QString pkceClientSecret() { return decodeCreds(kPkceSecretA, kPkceSecretB); }

    // Registered with Tidal for the PKCE client. It is a real tidal.com page
    // (it renders an "Oops"), not a localhost or custom-scheme URL, so the app
    // cannot intercept it — the user copies the URL back.
    static constexpr auto kPkceRedirect = "https://tidal.com/android/login/auth";
    static constexpr auto kPkceAuthUrl  = "https://login.tidal.com/authorize";
    static constexpr auto kCredsFile    = "/.config/tidal-wave/credentials.json";
};
