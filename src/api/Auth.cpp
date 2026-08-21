#include "Auth.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>

Auth::Auth(TidalApi *api, QObject *parent)
    : QObject(parent), m_api(api)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setSingleShot(false);
    connect(m_pollTimer, &QTimer::timeout, this, &Auth::pollForToken);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    connect(m_refreshTimer, &QTimer::timeout, this, &Auth::refreshAccessToken);

    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, &Auth::retrySession);
}

// The startup check is held off, not failed, while the network is missing;
// these bound how often it tries again.
static constexpr int kRetryFirstMs = 15 * 1000;
static constexpr int kRetryMaxMs   = 5 * 60 * 1000;

// 400 is what the token endpoint answers an invalid_grant with; 401/403 is
// either endpoint refusing the token. Anything else — 0 for a request that
// never landed, 5xx for Tidal being down, 400 from the API — is not a verdict
// on the credentials.
bool Auth::isGrantRejection(int httpStatus) {
    return httpStatus == 400 || isApiRejection(httpStatus);
}

bool Auth::isApiRejection(int httpStatus) {
    return httpStatus == 401 || httpStatus == 403;
}

// Tidal refusing the session ends the hold for real. Without this the retry
// timer keeps firing an old refresh token underneath whatever comes next — a
// PKCE login in progress, say — and the next hold starts at the backoff this
// one had reached.
void Auth::endHold() {
    m_retryTimer->stop();
    m_retryDelayMs = 0;
    setOffline(false);
}

void Auth::setOffline(bool v) {
    if (m_offline == v) return;
    m_offline = v;
    emit offlineChanged();
}

void Auth::retryNow() {
    if (m_accessToken.isEmpty() && m_refreshToken.isEmpty()) return;
    m_retryTimer->stop();
    m_retryDelayMs = 0;                 // asked for by hand: start over at 15s
    if (!m_refreshToken.isEmpty() && QDateTime::currentDateTime() >= m_tokenExpiry)
        refreshAccessToken();
    else
        fetchSession();
}

// Saved credentials that could not be checked are still the best thing we have:
// the stored identity is enough to run the app on cached and pinned content,
// and the check picks up again once the network is back. Dropping to the login
// page instead strands a signed-in user there, holding a valid refresh token.
void Auth::holdOffline(const QString &reason) {
    if (m_refreshToken.isEmpty() && m_accessToken.isEmpty()) {
        // Nothing to hold or retry with.
        endHold();
        emit loginFailed(reason);
        setState(State::LoggedOut);
        return;
    }

    setOffline(true);
    m_retryDelayMs = m_retryDelayMs == 0 ? kRetryFirstMs
                                         : qMin(m_retryDelayMs * 2, kRetryMaxMs);
    m_retryTimer->start(m_retryDelayMs);

    if (m_userId == 0) {
        // Tokens, but no session ever checked — a grant that landed just as the
        // network went. Keep them and stay on the flow's own screen: the retry,
        // or the notice's "Try again", finishes the sign-in instead of making
        // the user run the whole flow again. A startup check with no stored id
        // has no screen of its own, so that one falls back to the login page,
        // where the same notice offers the retry.
        emit loginFailed(reason);
        if (m_state == State::Restoring) setState(State::LoggedOut);
        return;
    }

    m_api->setAccessToken(m_accessToken);
    m_api->setCountryCode(m_countryCode);

    if (m_state != State::LoggedIn) {
        emit loginSucceeded();
        setState(State::LoggedIn);
    }
}

void Auth::retrySession() {
    // Only a session actually being held retries, and never underneath a user
    // who has gone back to the login page — "Try again" drives that one.
    if (!m_offline || m_state == State::LoggedOut) return;
    if (QDateTime::currentDateTime() >= m_tokenExpiry && !m_refreshToken.isEmpty())
        refreshAccessToken();
    else
        fetchSession();
}

void Auth::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

void Auth::startDeviceFlow() {
    if (m_state == State::PendingDevice) return;
    // Starting over drops any held session: its retry would otherwise fire the
    // old grant's tokens into the middle of this one.
    endHold();
    setState(State::PendingDevice);

    QUrlQuery form;
    form.addQueryItem("client_id", kClientId);
    form.addQueryItem("scope", "r_usr w_usr w_sub");

    m_api->postForm("oauth2/device_authorization", form,
        [this](QJsonObject obj, QString err) {
            if (!err.isEmpty()) {
                setState(State::LoggedOut);
                emit loginFailed(err);
                return;
            }
            m_deviceCode      = obj["deviceCode"].toString();
            m_userCode        = obj["userCode"].toString();
            QString uri       = obj["verificationUriComplete"].toString(
                                obj["verificationUri"].toString());
            if (!uri.isEmpty() && !uri.startsWith("http"))
                uri = "https://" + uri;
            m_verificationUri = uri;
            m_pollInterval    = obj["interval"].toInt(5);
            emit userCodeChanged();
            m_pollTimer->start(m_pollInterval * 1000);
        });
}

QString Auth::decodeCreds(const char *a, const char *b) {
    return QString::fromUtf8(QByteArray::fromBase64(
        QByteArray::fromBase64(QByteArray(a)) + QByteArray::fromBase64(QByteArray(b))));
}

void Auth::startPkceFlow() {
    if (m_state == State::PendingPkce) return;
    // As in startDeviceFlow(): a new login never inherits a held retry.
    endHold();

    // RFC 7636 S256: 32 random bytes, base64url, no padding.
    QByteArray raw(32, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32 *>(raw.data()), raw.size() / sizeof(quint32));
    m_codeVerifier = QString::fromUtf8(
        raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

    const QString challenge = QString::fromUtf8(
        QCryptographicHash::hash(m_codeVerifier.toUtf8(), QCryptographicHash::Sha256)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

    m_clientUniqueKey = QString::number(QRandomGenerator::system()->generate64(), 16);

    QUrlQuery q;
    q.addQueryItem("response_type",        "code");
    q.addQueryItem("redirect_uri",         kPkceRedirect);
    q.addQueryItem("client_id",            pkceClientId());
    q.addQueryItem("lang",                 "EN");
    q.addQueryItem("appMode",              "android");
    q.addQueryItem("client_unique_key",    m_clientUniqueKey);
    q.addQueryItem("code_challenge",       challenge);
    q.addQueryItem("code_challenge_method","S256");
    q.addQueryItem("restrict_signup",      "true");

    QUrl url(QString::fromLatin1(kPkceAuthUrl));
    url.setQuery(q);
    m_verificationUri = url.toString();
    m_userCode.clear();
    emit userCodeChanged();
    setState(State::PendingPkce);
}

void Auth::submitPkceRedirect(const QString &redirectUrl) {
    const QUrl url(redirectUrl.trimmed());
    const QUrlQuery q(url);

    if (q.hasQueryItem(QStringLiteral("error"))) {
        emit loginFailed(q.queryItemValue(QStringLiteral("error_description"),
                                          QUrl::FullyDecoded).isEmpty()
                         ? q.queryItemValue(QStringLiteral("error"))
                         : q.queryItemValue(QStringLiteral("error_description"),
                                            QUrl::FullyDecoded));
        return;
    }

    const QString code = q.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
    if (code.isEmpty()) {
        emit loginFailed(tr("That URL has no login code in it. Copy the whole address "
                            "of the page Tidal sent you to, including everything after "
                            "the '?'."));
        return;
    }

    QUrlQuery form;
    form.addQueryItem("code",              code);
    form.addQueryItem("client_id",         pkceClientId());
    form.addQueryItem("grant_type",        "authorization_code");
    form.addQueryItem("redirect_uri",      kPkceRedirect);
    form.addQueryItem("scope",             "r_usr w_usr w_sub");
    form.addQueryItem("code_verifier",     m_codeVerifier);
    form.addQueryItem("client_unique_key", m_clientUniqueKey);

    m_api->postForm("oauth2/token", form, [this](QJsonObject obj, QString err) {
        if (!err.isEmpty()) {
            emit loginFailed(err);
            return;
        }
        m_isPkce       = true;
        m_accessToken  = obj["access_token"].toString();
        m_refreshToken = obj["refresh_token"].toString();
        m_tokenExpiry  = QDateTime::currentDateTime().addSecs(obj["expires_in"].toInt(3600));
        m_api->setAccessToken(m_accessToken);
        fetchSession();
    });
}

void Auth::cancelDeviceFlow() {
    m_pollTimer->stop();
    // Cancel means a fresh start, so the hold and the tokens of the grant that
    // was being held both go.
    endHold();
    m_accessToken.clear();
    m_refreshToken.clear();
    m_deviceCode.clear();
    m_userCode.clear();
    m_verificationUri.clear();
    m_codeVerifier.clear();
    m_clientUniqueKey.clear();
    setState(State::LoggedOut);
}

void Auth::pollForToken() {
    QUrlQuery form;
    form.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    form.addQueryItem("device_code", m_deviceCode);
    form.addQueryItem("client_id", kClientId);
    form.addQueryItem("client_secret", kClientSecret);
    form.addQueryItem("scope", "r_usr w_usr w_sub");

    m_api->postForm("oauth2/token", form, [this](QJsonObject obj, QString err) {
        if (!err.isEmpty()) {
            // "authorization_pending" is normal - keep polling
            if (obj["error"].toString() == "authorization_pending") return;
            // "slow_down" means increase interval
            if (obj["error"].toString() == "slow_down") {
                m_pollInterval += 5;
                m_pollTimer->setInterval(m_pollInterval * 1000);
                return;
            }
            m_pollTimer->stop();
            setState(State::LoggedOut);
            emit loginFailed(err);
            return;
        }
        m_pollTimer->stop();
        m_isPkce       = false;
        m_accessToken  = obj["access_token"].toString();
        m_refreshToken = obj["refresh_token"].toString();
        m_tokenExpiry  = QDateTime::currentDateTime().addSecs(obj["expires_in"].toInt(3600));
        m_api->setAccessToken(m_accessToken);
        fetchSession();
    });
}

void Auth::refreshAccessToken() {
    if (m_refreshToken.isEmpty()) {
        emit sessionExpired();
        setState(State::LoggedOut);
        return;
    }
    // A refresh grant must present the same client the token was issued to;
    // crossing the pair gets the token rejected.
    QUrlQuery form;
    form.addQueryItem("grant_type", "refresh_token");
    form.addQueryItem("refresh_token", m_refreshToken);
    form.addQueryItem("client_id",     m_isPkce ? pkceClientId()     : QString::fromLatin1(kClientId));
    form.addQueryItem("client_secret", m_isPkce ? pkceClientSecret() : QString::fromLatin1(kClientSecret));

    m_api->postFormStatus("oauth2/token", form, [this](QJsonObject obj, QString err, int status) {
        if (!err.isEmpty()) {
            // Tidal turning the grant down ends the session; never reaching
            // Tidal does not.
            if (!isGrantRejection(status)) { holdOffline(err); return; }
            endHold();
            emit sessionExpired();
            setState(State::LoggedOut);
            return;
        }
        m_retryDelayMs = 0;
        m_accessToken = obj["access_token"].toString();
        if (obj.contains("refresh_token"))
            m_refreshToken = obj["refresh_token"].toString();
        m_tokenExpiry = QDateTime::currentDateTime().addSecs(obj["expires_in"].toInt(3600));
        m_api->setAccessToken(m_accessToken);
        saveCredentials();
        // A cold start whose access token expired while the app was closed
        // lands here with no userId and no country code, and the state is
        // still Restoring. fetchSession() fills those in, schedules the next
        // refresh and flips the state to LoggedIn; without it the app sits on
        // the login page holding a perfectly good token.
        // m_offline as well as the state: a session held through an outage
        // is already LoggedIn but was never checked, so its identity and its
        // user-scoped data are still missing. Only fetchSession() fills those
        // in and reports the recovery.
        if (m_state != State::LoggedIn || m_offline) {
            fetchSession();
            return;
        }
        // Schedule next refresh 60s before expiry
        qint64 msec = QDateTime::currentDateTime().msecsTo(m_tokenExpiry) - 60000;
        if (msec > 0) m_refreshTimer->start(msec);
    });
}

void Auth::fetchSession() {
    // Whatever flow got us here is over; a poll still running would hand a
    // second token in behind this session.
    m_pollTimer->stop();
    m_api->getStatus("sessions", {}, [this](QJsonObject obj, QString err, int status) {
        if (!err.isEmpty()) {
            if (!isApiRejection(status)) { holdOffline(err); return; }
            endHold();
            emit loginFailed(err);
            setState(State::LoggedOut);
            return;
        }
        m_retryDelayMs = 0;
        m_retryTimer->stop();
        m_userId      = obj["userId"].toVariant().toLongLong();
        m_countryCode = obj["countryCode"].toString();
        m_api->setCountryCode(m_countryCode);

        m_api->get(QStringLiteral("users/%1").arg(m_userId), {},
            [this](QJsonObject u, QString) {
                QString name = u["username"].toString();
                if (name.isEmpty()) {
                    QString first = u["firstName"].toString();
                    QString last  = u["lastName"].toString();
                    name = (first + " " + last).trimmed();
                }
                if (!name.isEmpty() && name != m_username) {
                    m_username = name;
                    emit usernameChanged();
                    saveCredentials();
                }
            });

        saveCredentials();

        // Schedule token refresh
        qint64 msec = QDateTime::currentDateTime().msecsTo(m_tokenExpiry) - 60000;
        if (msec > 0) m_refreshTimer->start(msec);

        // Emit before flipping state to LoggedIn: QML reacts to the state
        // change by immediately fetching user-scoped data (playlists, etc),
        // which requires TidalClient::userId to already be set via this signal.
        emit loginSucceeded();
        setState(State::LoggedIn);
        if (m_offline) {
            setOffline(false);
            emit sessionRecovered();
        }
    });
}

void Auth::loadCredentials() {
    QString path = QDir::homePath() + kCredsFile;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;

    auto doc = QJsonDocument::fromJson(f.readAll());
    auto obj = doc.object();
    m_accessToken  = obj["access_token"].toString();
    m_refreshToken = obj["refresh_token"].toString();
    m_tokenExpiry  = QDateTime::fromString(obj["expires_at"].toString(), Qt::ISODate);
    m_userId       = obj["user_id"].toVariant().toLongLong();
    m_countryCode  = obj["country_code"].toString();
    m_username     = obj["username"].toString();
    m_isPkce       = obj["is_pkce"].toBool(false);

    if (m_accessToken.isEmpty() || m_refreshToken.isEmpty()) return;

    // Saved tokens exist, so treat the user as logged in until the server says
    // otherwise. Staying LoggedOut here puts the login page on screen for the
    // length of the session check, every launch.
    setState(State::Restoring);

    m_api->setAccessToken(m_accessToken);
    m_api->setCountryCode(m_countryCode);

    // If token already expired, refresh immediately
    if (QDateTime::currentDateTime() >= m_tokenExpiry) {
        refreshAccessToken();
    } else {
        // Validate session
        fetchSession();
        qint64 msec = QDateTime::currentDateTime().msecsTo(m_tokenExpiry) - 60000;
        if (msec > 0) m_refreshTimer->start(msec);
    }
}

void Auth::saveCredentials() {
    QString dir = QDir::homePath() + "/.config/tidal-wave";
    QDir().mkpath(dir);
    QString path = dir + "/credentials.json";

    QJsonObject obj;
    obj["access_token"]  = m_accessToken;
    obj["refresh_token"] = m_refreshToken;
    obj["expires_at"]    = m_tokenExpiry.toString(Qt::ISODate);
    obj["user_id"]       = m_userId;
    obj["country_code"]  = m_countryCode;
    obj["username"]      = m_username;
    obj["is_pkce"]       = m_isPkce;

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        f.write(QJsonDocument(obj).toJson());
    }
}

void Auth::clearCredentials() {
    QFile::remove(QDir::homePath() + kCredsFile);
}

void Auth::logout() {
    m_pollTimer->stop();
    m_refreshTimer->stop();
    endHold();
    m_accessToken.clear();
    m_refreshToken.clear();
    m_deviceCode.clear();
    m_userCode.clear();
    m_countryCode.clear();
    m_codeVerifier.clear();
    m_clientUniqueKey.clear();
    m_isPkce = false;
    m_userId = 0;
    m_api->setAccessToken({});
    clearCredentials();
    setState(State::LoggedOut);
}
