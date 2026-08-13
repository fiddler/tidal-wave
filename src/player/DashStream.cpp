#include "DashStream.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QElapsedTimer>
#include <QThread>

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include <cstring>
#include <map>

#include "DashFetcher.h"

// ─── DashNet ───────────────────────────────────────────────────────────────

DashNet::DashNet(QObject *parent) : QObject(parent) {}

DashNet *DashNet::instance() {
    static DashNet *net = nullptr;
    static QThread *thread = nullptr;
    if (!net) {
        thread = new QThread;
        thread->setObjectName(QStringLiteral("dash-net"));
        net = new DashNet;
        net->moveToThread(thread);
        thread->start();
    }
    return net;
}

void DashNet::request(const QUrl &url, qint64 first, qint64 last, Callback cb) {
    // A lambda, not Q_ARG: std::function has no metatype to marshal through.
    QMetaObject::invokeMethod(this, [this, url, first, last, cb = std::move(cb)]() {
        doRequest(url, first, last, cb);
    }, Qt::QueuedConnection);
}

void DashNet::doRequest(const QUrl &url, qint64 first, qint64 last, const Callback &cb) {
    // Created lazily so it belongs to this thread, not whoever constructed us.
    if (!m_nam) m_nam = new QNetworkAccessManager(this);

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    if (last >= 0)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(first)
                                  + "-" + QByteArray::number(last));

    QNetworkReply *reply = m_nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            cb({}, -1, reply->errorString());
            return;
        }
        // A 206 reports the body length in Content-Length; the size of the whole
        // segment is only in Content-Range ("bytes 0-16383/527896").
        qint64 total = -1;
        const QByteArray cr = reply->rawHeader("Content-Range");
        const int slash = cr.lastIndexOf('/');
        if (slash >= 0) total = cr.mid(slash + 1).trimmed().toLongLong();
        if (total <= 0)
            total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        cb(reply->readAll(), total, QString());
    });
}

// ─── registry ──────────────────────────────────────────────────────────────

namespace {

std::mutex &registryMutex() {
    static std::mutex m;
    return m;
}

std::map<quint64, std::shared_ptr<DashStream>> &registry() {
    static std::map<quint64, std::shared_ptr<DashStream>> r;
    return r;
}

quint64 nextStreamId() {
    static quint64 n = 0;
    return ++n;
}

} // namespace

// ─── DashStream ────────────────────────────────────────────────────────────

static QElapsedTimer g_clock;

DashStream::DashStream(quint64 id, QStringList urls)
    : m_id(id), m_urls(std::move(urls))
{
    g_clock.start();
    const int n = m_urls.size();
    m_head.resize(n);
    m_data.resize(n);
    m_size.assign(n, -1);
    m_offset.assign(n, -1);
    m_inFlight.assign(n, false);
}

DashStream::~DashStream() = default;

std::shared_ptr<DashStream> DashStream::create(const QString &mpdXml) {
    QStringList urls = DashFetcher::parseSegmentUrls(mpdXml);
    if (urls.isEmpty()) return nullptr;

    const quint64 id = nextStreamId();
    std::shared_ptr<DashStream> s(new DashStream(id, std::move(urls)));
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        registry()[id] = s;
    }
    s->startProbe();
    return s;
}

void DashStream::retire(const std::shared_ptr<DashStream> &s) {
    if (!s) return;
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        registry().erase(s->m_id);
    }
    {
        std::lock_guard<std::mutex> lock(s->m_mutex);
        s->m_aborted = true;
    }
    s->m_cv.notify_all();   // release a reader blocked on a segment
}

QUrl DashStream::url() const {
    // The id goes in the path, not the authority: QUrl normalises a bare
    // numeric host into an IP address, so "tidalstream://1" came back out as
    // "tidalstream://0.0.0.1" and the lookup missed.
    return QUrl(QStringLiteral("tidalstream://s/%1").arg(m_id));
}

void DashStream::startProbe() {
    // One small ranged GET per segment. It does two jobs at once: Content-Range
    // reveals the segment's full size, so the byte offsets — and therefore
    // seeking — are exact, and the returned bytes are the fragment's moof
    // header.
    //
    // That header is the point. When ffmpeg opens a fragmented MP4 it walks
    // every fragment boundary to build its index. Measured cold, each of those
    // small reads pulled a whole 512 KB segment and first audio took ~7 s.
    // Serving them from here keeps the scan off the network completely.
    auto self = shared_from_this();
    for (int i = 0; i < m_urls.size(); ++i) {
        DashNet::instance()->request(QUrl(m_urls.at(i)), 0, kHead - 1,
            [self, i](QByteArray body, qint64 size, QString err) {
                {
                    std::lock_guard<std::mutex> lock(self->m_mutex);
                    self->m_head[i] = body;
                    self->m_size[i] = err.isEmpty() && size > 0 ? size : 0;
                    if (!err.isEmpty() && self->m_error.isEmpty()) self->m_error = err;
                    if (++self->m_probed == self->m_urls.size()) {
                        qint64 run = 0;
                        for (int k = 0; k < self->m_urls.size(); ++k) {
                            self->m_offset[k] = run;
                            run += self->m_size[k];
                        }
                        self->m_total = run;
                        qInfo() << "[dash] ready in" << g_clock.elapsed()
                                << "ms," << self->m_urls.size() << "segments,"
                                << run << "bytes";
                    }
                }
                self->m_cv.notify_all();
            });
    }

    // Pull the opening segments in full while the probe runs, so playback has
    // real audio to start on the moment the offsets are known.
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = 0; i < qMin<qsizetype>(2, m_urls.size()); ++i)
        ensureSegment(i);
}

void DashStream::ensureSegment(int index) {
    if (index < 0 || index >= m_urls.size()) return;
    if (!m_data[index].isEmpty() || m_inFlight[index]) return;

    m_inFlight[index] = true;
    auto self = shared_from_this();
    DashNet::instance()->request(QUrl(m_urls.at(index)), 0, -1,
        [self, index](QByteArray body, qint64, QString err) {
            {
                std::lock_guard<std::mutex> lock(self->m_mutex);
                self->m_inFlight[index] = false;
                if (err.isEmpty() && !body.isEmpty()) self->m_data[index] = body;
                else if (self->m_error.isEmpty())     self->m_error = err;
            }
            self->m_cv.notify_all();
        });
}

void DashStream::evictFar(int keepAround) {
    for (int i = 0; i < m_data.size(); ++i) {
        if (m_data[i].isEmpty()) continue;
        if (i < keepAround - kWindow / 2 || i > keepAround + kWindow)
            m_data[i] = QByteArray();
    }
}

qint64 DashStream::totalSize() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return m_total >= 0 || m_aborted; });
    return m_aborted ? MPV_ERROR_UNSUPPORTED : m_total;
}

qint64 DashStream::seekTo(qint64 offset) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return m_total >= 0 || m_aborted; });
    if (m_aborted || offset < 0 || offset > m_total) return MPV_ERROR_UNSUPPORTED;
    m_pos = offset;
    return m_pos;
}

qint64 DashStream::readAt(char *buf, qint64 want) {
    std::unique_lock<std::mutex> lock(m_mutex);

    // The offsets come from the probe, so wait for it before mapping m_pos.
    m_cv.wait(lock, [this] { return m_total >= 0 || m_aborted; });
    if (m_aborted) return -1;
    if (m_pos >= m_total) return 0;                     // clean EOF

    int idx = 0;                                        // segment holding m_pos
    while (idx + 1 < m_urls.size() && m_offset[idx + 1] <= m_pos) ++idx;
    const qint64 within = m_pos - m_offset[idx];

    // Index-scan reads land inside the cached head. Serving them there costs no
    // network at all; a short read is legal, so no need to satisfy `want` fully.
    if (m_data[idx].isEmpty() && within < m_head[idx].size()) {
        const qint64 n = qMin(want, qint64(m_head[idx].size()) - within);
        memcpy(buf, m_head[idx].constData() + within, n);
        m_pos += n;
        return n;
    }

    ensureSegment(idx);
    for (int ahead = 1; ahead <= 2; ++ahead)            // keep the pipe warm
        ensureSegment(idx + ahead);

    m_cv.wait(lock, [this, idx] {
        return !m_data[idx].isEmpty() || m_aborted
               || (!m_inFlight[idx] && !m_error.isEmpty());
    });
    if (m_aborted) return -1;
    if (m_data[idx].isEmpty()) return -1;               // fetch failed

    const qint64 avail = m_data[idx].size() - within;
    if (avail <= 0) return 0;
    const qint64 n = qMin(want, avail);
    memcpy(buf, m_data[idx].constData() + within, n);
    m_pos += n;

    evictFar(idx);
    return n;
}

// ─── mpv glue ──────────────────────────────────────────────────────────────

namespace {

// The cookie owns a reference, so the stream outlives a retire() while mpv is
// still reading from it.
using Cookie = std::shared_ptr<DashStream>;

int64_t cbRead(void *cookie, char *buf, uint64_t nbytes) {
    auto *sp = static_cast<Cookie *>(cookie);
    return (*sp)->readAt(buf, static_cast<qint64>(nbytes));
}

int64_t cbSeek(void *cookie, int64_t offset) {
    auto *sp = static_cast<Cookie *>(cookie);
    return (*sp)->seekTo(offset);
}

int64_t cbSize(void *cookie) {
    auto *sp = static_cast<Cookie *>(cookie);
    return (*sp)->totalSize();
}

void cbClose(void *cookie) {
    delete static_cast<Cookie *>(cookie);
}

int cbOpen(void *, char *uri, mpv_stream_cb_info *info) {
    const QString s = QString::fromUtf8(uri);
    const quint64 id = QStringView{s}.mid(s.lastIndexOf(QLatin1Char('/')) + 1).toULongLong();

    std::shared_ptr<DashStream> stream;
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        auto it = registry().find(id);
        if (it == registry().end()) return MPV_ERROR_LOADING_FAILED;
        stream = it->second;
    }

    info->cookie  = new Cookie(std::move(stream));
    info->read_fn = cbRead;
    info->seek_fn = cbSeek;
    info->size_fn = cbSize;
    info->close_fn = cbClose;
    return 0;
}

} // namespace

void DashStream::install(mpv_handle *mpv) {
    if (!mpv) return;
    mpv_stream_cb_add_ro(mpv, "tidalstream", nullptr, cbOpen);
}
