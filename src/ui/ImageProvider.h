#pragma once
#include <QQuickAsyncImageProvider>
#include <QImage>

class TidalImageProvider : public QQuickAsyncImageProvider {
public:
    explicit TidalImageProvider();
    QQuickImageResponse *requestImageResponse(
        const QString &id, const QSize &requestedSize) override;

    // Shared disk cache for remote artwork. Tidal image URLs are
    // content-addressed (UUID + size in the path), so a URL-keyed file cache
    // never goes stale. Also written by OfflineManager, which pre-fetches the
    // covers of pinned tracks so they render with no network.
    static QString cachePathFor(const QUrl &url);
    static void    storeInCache(const QUrl &url, const QByteArray &data);
};

class ImageResponse : public QQuickImageResponse {
    Q_OBJECT
public:
    ImageResponse(const QUrl &url, const QSize &size);
    QQuickTextureFactory *textureFactory() const override;

private:
    void decode(QByteArray &data);

    QSize  m_size;
    QImage m_image;
};
