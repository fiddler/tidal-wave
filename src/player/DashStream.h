#pragma once
#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

struct mpv_handle;
class QNetworkAccessManager;

// One background Qt thread with its own QNetworkAccessManager.
//
// mpv's stream callbacks are blocking and run on mpv's own threads. If those
// waited on the GUI thread, any mpv call the GUI thread makes could deadlock
// against them. Servicing them from a dedicated thread keeps the GUI thread out
// of the loop entirely. Callbacks run on this thread, not the caller's.
class DashNet : public QObject {
    Q_OBJECT
public:
    static DashNet *instance();

    // totalSize is the size of the whole resource, not of the returned body:
    // for a 206 it comes from Content-Range, so a ranged read still learns how
    // big the segment is.
    using Callback = std::function<void(QByteArray body, qint64 totalSize, QString error)>;
    // last < 0 means "no Range header": fetch the whole thing.
    void request(const QUrl &url, qint64 first, qint64 last, Callback cb);

private:
    explicit DashNet(QObject *parent = nullptr);
    void doRequest(const QUrl &url, qint64 first, qint64 last, const Callback &cb);

    QNetworkAccessManager *m_nam = nullptr;   // created on, and used from, the thread
};

// A Tidal DASH stream presented to mpv as one seekable file.
//
// The segments are plain fragmented-MP4 pieces, so their concatenation is an
// ordinary MP4. Rather than downloading all of it before playback — about 32 MB
// and several seconds — this serves the bytes on demand: mpv asks for a range,
// and only the segments covering that range are fetched. Playback starts after
// the init segment plus one media segment, roughly 1 MB.
//
// The byte offsets of the segments are learned up front with one HEAD per
// segment (headers only), which is what lets seeking stay exact.
//
// Lifetime is shared: the registry holds one reference and each open mpv stream
// holds another, so a track change can retire a stream while mpv is still
// reading it without freeing anything under mpv's feet.
class DashStream : public std::enable_shared_from_this<DashStream> {
public:
    // Registers the "tidalstream" protocol with an mpv instance. Call once.
    static void install(mpv_handle *mpv);

    // Returns null when the manifest has no usable segments.
    static std::shared_ptr<DashStream> create(const QString &mpdXml);
    // Drops the registry's reference and wakes any blocked reader.
    static void retire(const std::shared_ptr<DashStream> &s);

    ~DashStream();

    // The URL to hand to mpv.
    QUrl url() const;

    // Called on mpv's threads, and allowed to block — that is the contract
    // mpv_stream_cb_add_ro defines. They are public only so the C callbacks
    // can reach them; nothing else should call them.
    qint64 readAt(char *buf, qint64 want);
    qint64 seekTo(qint64 offset);
    qint64 totalSize();

private:
    DashStream(quint64 id, QStringList urls);

    void startProbe();
    void ensureSegment(int index);           // caller holds m_mutex
    void evictFar(int keepAround);           // caller holds m_mutex

    static constexpr int kWindow = 8;        // full segments kept cached, ~4 MB
    // Enough to cover a fragment's moof header. ffmpeg walks every fragment
    // boundary when it opens a fragmented MP4; serving those reads from here
    // keeps the scan off the network entirely.
    static constexpr qint64 kHead = 16384;

    const quint64 m_id;
    const QStringList m_urls;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;

    std::vector<QByteArray> m_head;          // first kHead bytes of each segment
    std::vector<QByteArray> m_data;          // full segment bodies, fetched on demand
    std::vector<qint64>     m_size;          // per-segment size, -1 until probed
    std::vector<qint64>     m_offset;        // start offset, valid once probed
    std::vector<bool>       m_inFlight;

    qint64 m_total    = -1;                  // -1 until the probe completes
    qint64 m_pos      = 0;                   // current read offset
    int    m_probed   = 0;
    bool   m_aborted  = false;
    QString m_error;
};
