#pragma once
#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>

class QNetworkReply;
class QTemporaryFile;
class TidalClient;

// Tidal serves LOSSLESS and HI_RES_LOSSLESS as MPEG-DASH: a manifest plus one
// URL per segment. Qt's macOS (AVFoundation) backend cannot read a manifest, so
// handing it the .mpd gives a track stuck at 0:00.
//
// The segments are plain fragmented-MP4 pieces, so joining the initialization
// segment and every media segment in order produces one ordinary FLAC-in-MP4
// file the backend plays without help. That is what this class does — it turns
// a DASH manifest into the same "one local temp file" shape the BTS (AAC) path
// already produces, so playback stays a single code path.
class DashFetcher : public QObject {
    Q_OBJECT
public:
    DashFetcher(TidalClient *client, const QString &mpdXml, QObject *parent = nullptr);
    ~DashFetcher() override;

    // Expands a Tidal DASH manifest into segment URLs: [0] is the
    // initialization segment, the rest are the media segments in play order.
    // Empty when the manifest carries no usable SegmentTemplate.
    static QStringList parseSegmentUrls(const QString &mpdXml);

    // True when the manifest yielded at least one segment URL.
    bool isValid() const { return !m_urls.isEmpty(); }

    void start();
    // Cancels every in-flight request. No signal is emitted afterwards.
    void abort();

signals:
    // file is null when error is non-empty. Ownership passes to the receiver.
    void finished(QTemporaryFile *file, const QString &error);

private:
    void fetchAt(int index);
    void maybeComplete();

    TidalClient           *m_client;
    QStringList            m_urls;        // [0] is the initialization segment
    QVector<QByteArray>    m_parts;       // filled out of order, written in order
    // QPointer, not a raw pointer: TidalApi deleteLater()s every reply as it
    // finishes, so a plain list would fill with dangling pointers and abort()
    // would walk them.
    QVector<QPointer<QNetworkReply>> m_inFlight;
    int   m_nextToRequest = 0;
    int   m_completed     = 0;
    bool  m_aborted       = false;
    bool  m_failed        = false;

    // Segments are small and independent, so a few run at once. Sequential
    // fetching would add one round trip per segment — about 50 of them on a
    // normal track — before the first note plays.
    static constexpr int kMaxParallel = 6;
};
