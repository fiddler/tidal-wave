#include "ImageProvider.h"
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QQuickTextureFactory>
#include <QImageReader>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

TidalImageProvider::TidalImageProvider()
    : QQuickAsyncImageProvider()
{}

QQuickImageResponse *TidalImageProvider::requestImageResponse(
    const QString &id, const QSize &requestedSize)
{
    // Cover art for local library tracks arrives as a file: URL (QNetworkAccessManager
    // serves those directly); everything else is a bare Tidal resources host + path.
    QUrl url(id.startsWith("http") || id.startsWith("file:") ? id : ("https://" + id));
    return new ::ImageResponse(url, requestedSize);
}

QString TidalImageProvider::cachePathFor(const QUrl &url) {
    static const QString dir = [] {
        const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                          + QStringLiteral("/artcache");
        QDir().mkpath(d);
        return d;
    }();
    const QByteArray hash = QCryptographicHash::hash(
        url.toString().toUtf8(), QCryptographicHash::Sha1).toHex();
    return dir + QStringLiteral("/") + QString::fromLatin1(hash);
}

void TidalImageProvider::storeInCache(const QUrl &url, const QByteArray &data) {
    if (data.isEmpty()) return;
    QFile f(cachePathFor(url));
    if (f.open(QIODevice::WriteOnly)) f.write(data);
}

// ─── ImageResponse ─────────────────────────────────

ImageResponse::ImageResponse(const QUrl &url, const QSize &size)
    : m_size(size)
{
    // Disk-cache hit: decode locally, no network. The emit is deferred because
    // the pixmap reader connects to finished() only after this constructor
    // returns — a synchronous emit would be lost and the image would hang.
    if (url.scheme() != QLatin1String("file")) {
        QFile cached(TidalImageProvider::cachePathFor(url));
        if (cached.open(QIODevice::ReadOnly)) {
            QByteArray data = cached.readAll();
            decode(data);
            QMetaObject::invokeMethod(this, [this]() { emit finished(); },
                                      Qt::QueuedConnection);
            return;
        }
    }

    // Create QNAM on the calling thread (QQuickPixmapReader) so there's no
    // cross-thread parent/child relationship when the reply is created.
    auto *nam = new QNetworkAccessManager();
    auto *reply = nam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, url, reply, nam]() {
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            if (url.scheme() != QLatin1String("file"))
                TidalImageProvider::storeInCache(url, data);
            decode(data);
        }
        emit finished();
    });
}

void ImageResponse::decode(QByteArray &data) {
    QBuffer buf(&data);
    QImageReader reader(&buf);
    if (m_size.isValid()) reader.setScaledSize(m_size);
    m_image = reader.read();
}

QQuickTextureFactory *ImageResponse::textureFactory() const {
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}
