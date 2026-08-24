#include "ImageProvider.h"
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QQuickTextureFactory>
#include <QImageReader>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
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

QString TidalImageProvider::cacheDir() {
    static const QString dir = [] {
        const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                          + QStringLiteral("/artcache");
        QDir().mkpath(d);
        return d;
    }();
    return dir;
}

QString TidalImageProvider::cachePathFor(const QUrl &url) {
    const QByteArray hash = QCryptographicHash::hash(
        url.toString().toUtf8(), QCryptographicHash::Sha1).toHex();
    return cacheDir() + QStringLiteral("/") + QString::fromLatin1(hash);
}

qint64 TidalImageProvider::cacheBytes() {
    qint64 total = 0;
    QDirIterator it(cacheDir(), QDir::Files);
    while (it.hasNext()) { it.next(); total += it.fileInfo().size(); }
    return total;
}

int TidalImageProvider::clearCache() {
    // The suffix check is the guard: cacheDir() is built from
    // AppDataLocation, and an empty or unexpected value there would otherwise
    // point this loop at a directory that is not ours to empty.
    const QString dir = cacheDir();
    if (!dir.endsWith(QStringLiteral("/artcache"))) {
        qWarning() << "[artcache] refusing to clear unexpected path:" << dir;
        return 0;
    }
    int removed = 0;
    QDirIterator it(dir, QDir::Files);
    while (it.hasNext()) {
        it.next();
        if (QFile::remove(it.filePath())) ++removed;
    }
    return removed;
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
