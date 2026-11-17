#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <functional>

class TidalApi : public QObject {
    Q_OBJECT
public:
    using JsonCallback = std::function<void(QJsonObject, QString /*error*/)>;
    using RawCallback  = std::function<void(QByteArray, QString /*error*/)>;
    using ArrayCallback = std::function<void(QJsonArray, QString /*error*/)>;
    // Same as JsonCallback plus the status the reply carried. Status 0 means
    // the request never reached Tidal at all — offline, blocked, DNS — which
    // says nothing about the credentials it was sent with.
    using JsonStatusCallback = std::function<void(QJsonObject, QString /*error*/, int /*httpStatus*/)>;

    explicit TidalApi(QObject *parent = nullptr);

    void setAccessToken(const QString &token);
    void setCountryCode(const QString &cc);
    QString countryCode() const { return m_countryCode; }

    // Low-level GET/POST
    void get(const QString &endpoint, const QUrlQuery &params, JsonCallback cb);
    void post(const QString &endpoint, const QByteArray &body,
              const QMap<QString,QString> &extraHeaders, JsonCallback cb);
    void postForm(const QString &endpoint, const QUrlQuery &form, JsonCallback cb);
    // Status-carrying variants, for callers that must tell "Tidal said no"
    // from "Tidal was never reached".
    void getStatus(const QString &endpoint, const QUrlQuery &params, JsonStatusCallback cb);
    // For the few endpoints whose reply is a bare JSON array (track credits).
    void getArray(const QString &endpoint, const QUrlQuery &params, ArrayCallback cb);
    // The v2 host. Saved mixes live only there.
    void getV2(const QString &endpoint, const QUrlQuery &params, JsonCallback cb);
    void putV2(const QString &endpoint, const QUrlQuery &params, JsonCallback cb);
    void postFormStatus(const QString &endpoint, const QUrlQuery &form, JsonStatusCallback cb);
    // POST against the authenticated API host (api.tidal.com), unlike post()/postForm()
    // which target the OAuth host (auth.tidal.com) and deliberately omit auth headers.
    void postApiForm(const QString &endpoint, const QUrlQuery &form, JsonCallback cb);
    void deleteApi(const QString &endpoint, const QUrlQuery &params, JsonCallback cb);
    void getEtag(const QString &endpoint, std::function<void(QString /*etag*/, QString /*error*/)> cb);
    void deleteApiEtag(const QString &endpoint, const QUrlQuery &params, const QString &etag, JsonCallback cb);
    void postApiFormEtag(const QString &endpoint, const QUrlQuery &form, const QString &etag, JsonCallback cb);
    QNetworkReply* getRaw(const QUrl &url, RawCallback cb);

    static constexpr auto kApiBase  = "https://api.tidal.com/v1/";
    static constexpr auto kApiBaseV2 = "https://api.tidal.com/v2/";
    // An idle timeout, not a budget: Qt aborts a transfer only after this long
    // with no bytes moving, so a long download that keeps flowing is untouched.
    // Without it a request that connects and then goes quiet never finishes,
    // and whatever spinner waits on its callback spins for the life of the app.
    static constexpr int kTransferTimeoutMs = 15000;
    static constexpr auto kAuthBase = "https://auth.tidal.com/v1/";

    // Public for tests. The sign-in token goes to Tidal's API host and
    // nowhere else: audio and covers come from other hosts, by signed or
    // public URL.
    QNetworkRequest makeRequest(const QUrl &url);
    static bool sendsTokenTo(const QUrl &url);

private:
    QNetworkAccessManager *m_nam;
    QString m_accessToken;
    QString m_countryCode;


    void getDoc(const QString &base, const QString &endpoint, const QUrlQuery &params,
                std::function<void(QJsonDocument, QString, int)> cb);
    void postStatus(const QString &endpoint, const QByteArray &body,
                    const QMap<QString,QString> &extraHeaders, JsonStatusCallback cb);
};
