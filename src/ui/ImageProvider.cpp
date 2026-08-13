#include "ImageProvider.h"
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QQuickTextureFactory>
#include <QImageReader>
#include <QBuffer>

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

// ─── ImageResponse ─────────────────────────────────

ImageResponse::ImageResponse(const QUrl &url, const QSize &size)
    : m_size(size)
{
    // Create QNAM on the calling thread (QQuickPixmapReader) so there's no
    // cross-thread parent/child relationship when the reply is created.
    auto *nam = new QNetworkAccessManager();
    auto *reply = nam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QBuffer buf(&data);
            QImageReader reader(&buf);
            if (m_size.isValid()) reader.setScaledSize(m_size);
            m_image = reader.read();
        }
        emit finished();
    });
}

QQuickTextureFactory *ImageResponse::textureFactory() const {
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}
