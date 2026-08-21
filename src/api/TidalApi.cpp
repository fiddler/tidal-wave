#include "TidalApi.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

TidalApi::TidalApi(QObject *parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

void TidalApi::setAccessToken(const QString &token) { m_accessToken = token; }
void TidalApi::setCountryCode(const QString &cc)    { m_countryCode = cc; }

QNetworkRequest TidalApi::makeRequest(const QUrl &url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 KHTML, like Gecko Chrome/131 Safari/537.36");
    req.setRawHeader("X-Tidal-Token", "fX2JxdmntZWK0ixT");
    if (!m_accessToken.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_accessToken).toUtf8());
    return req;
}

// The status a finished reply carries, or 0 when it never got one.
static int httpStatusOf(QNetworkReply *reply) {
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

void TidalApi::get(const QString &endpoint, const QUrlQuery &params, JsonCallback cb) {
    getStatus(endpoint, params, [cb](QJsonObject obj, QString err, int) { cb(obj, err); });
}

void TidalApi::getStatus(const QString &endpoint, const QUrlQuery &params, JsonStatusCallback cb) {
    QUrl url(kApiBase + endpoint);
    QUrlQuery q = params;
    if (!m_countryCode.isEmpty()) q.addQueryItem("countryCode", m_countryCode);
    url.setQuery(q);

    auto *reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        const int status = httpStatusOf(reply);
        if (reply->error() != QNetworkReply::NoError) {
            cb({}, reply->errorString(), status);
            return;
        }
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(reply->readAll(), &err);
        if (err.error != QJsonParseError::NoError) {
            cb({}, err.errorString(), status);
            return;
        }
        cb(doc.object(), {}, status);
    });
}

void TidalApi::post(const QString &endpoint, const QByteArray &body,
                    const QMap<QString,QString> &extraHeaders, JsonCallback cb) {
    postStatus(endpoint, body, extraHeaders,
        [cb](QJsonObject obj, QString err, int) { cb(obj, err); });
}

void TidalApi::postStatus(const QString &endpoint, const QByteArray &body,
                          const QMap<QString,QString> &extraHeaders, JsonStatusCallback cb) {
    QUrl url(kAuthBase + endpoint);
    // Auth endpoints must NOT receive X-Tidal-Token or Authorization headers
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 KHTML, like Gecko Chrome/131 Safari/537.36");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    for (auto it = extraHeaders.cbegin(); it != extraHeaders.cend(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());

    auto *reply = m_nam->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        const int status = httpStatusOf(reply);
        QByteArray data = reply->readAll();
        // A request that never landed has no body to read a reason out of, so
        // report the transport failure rather than a JSON parse error.
        if (data.isEmpty() && reply->error() != QNetworkReply::NoError) {
            cb({}, reply->errorString(), status);
            return;
        }
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) {
            cb({}, err.errorString(), status);
            return;
        }
        auto obj = doc.object();
        if (obj.contains("error"))
            cb(obj, obj["error_description"].toString(obj["error"].toString()), status);
        else
            cb(obj, {}, status);
    });
}

void TidalApi::postForm(const QString &endpoint, const QUrlQuery &form, JsonCallback cb) {
    post(endpoint, form.toString(QUrl::FullyEncoded).toUtf8(), {}, cb);
}

void TidalApi::postFormStatus(const QString &endpoint, const QUrlQuery &form, JsonStatusCallback cb) {
    postStatus(endpoint, form.toString(QUrl::FullyEncoded).toUtf8(), {}, cb);
}

void TidalApi::postApiForm(const QString &endpoint, const QUrlQuery &form, JsonCallback cb) {
    QUrl url(kApiBase + endpoint);
    QNetworkRequest req = makeRequest(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    auto *reply = m_nam->post(req, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        if (data.isEmpty()) {
            cb({}, reply->error() == QNetworkReply::NoError ? QString() : reply->errorString());
            return;
        }
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) {
            cb({}, err.errorString());
            return;
        }
        auto obj = doc.object();
        if (obj.contains("error"))
            cb(obj, obj["error_description"].toString(obj["error"].toString()));
        else
            cb(obj, {});
    });
}

void TidalApi::deleteApi(const QString &endpoint, const QUrlQuery &params, JsonCallback cb) {
    QUrl url(kApiBase + endpoint);
    if (!params.isEmpty()) {
        url.setQuery(params);
    }
    QNetworkRequest req = makeRequest(url);
    auto *reply = m_nam->deleteResource(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        if (data.isEmpty()) {
            cb({}, reply->error() == QNetworkReply::NoError ? QString() : reply->errorString());
            return;
        }
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) {
            cb({}, err.errorString());
            return;
        }
        cb(doc.object(), {});
    });
}

void TidalApi::getEtag(const QString &endpoint, std::function<void(QString, QString)> cb) {
    QUrl url(kApiBase + endpoint);
    if (!m_countryCode.isEmpty()) {
        QUrlQuery q;
        q.addQueryItem("countryCode", m_countryCode);
        url.setQuery(q);
    }
    auto *reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            cb({}, reply->errorString());
            return;
        }
        QString etag = QString::fromLatin1(reply->rawHeader("ETag"));
        cb(etag, {});
    });
}

void TidalApi::deleteApiEtag(const QString &endpoint, const QUrlQuery &params, const QString &etag, JsonCallback cb) {
    QUrl url(kApiBase + endpoint);
    if (!params.isEmpty()) url.setQuery(params);
    QNetworkRequest req = makeRequest(url);
    if (!etag.isEmpty())
        req.setRawHeader("If-None-Match", etag.toLatin1());
    auto *reply = m_nam->deleteResource(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        if (data.isEmpty()) {
            cb({}, reply->error() == QNetworkReply::NoError ? QString() : reply->errorString());
            return;
        }
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) { cb({}, err.errorString()); return; }
        cb(doc.object(), {});
    });
}

void TidalApi::postApiFormEtag(const QString &endpoint, const QUrlQuery &form, const QString &etag, JsonCallback cb) {
    QUrl url(kApiBase + endpoint);
    QNetworkRequest req = makeRequest(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    if (!etag.isEmpty())
        req.setRawHeader("If-None-Match", etag.toLatin1());
    auto *reply = m_nam->post(req, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        if (data.isEmpty()) {
            cb({}, reply->error() == QNetworkReply::NoError ? QString() : reply->errorString());
            return;
        }
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) { cb({}, err.errorString()); return; }
        auto obj = doc.object();
        if (obj.contains("error"))
            cb(obj, obj["error_description"].toString(obj["error"].toString()));
        else
            cb(obj, {});
    });
}

QNetworkReply* TidalApi::getRaw(const QUrl &url, RawCallback cb) {
    auto *reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            cb({}, reply->errorString());
            return;
        }
        cb(reply->readAll(), {});
    });
    return reply;
}
