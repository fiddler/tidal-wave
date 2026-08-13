#include "DashFetcher.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QTemporaryFile>
#include <QXmlStreamReader>

#include "TidalClient.h"

QStringList DashFetcher::parseSegmentUrls(const QString &mpdXml) {
    // Tidal's manifests carry exactly one audio Representation with a
    // SegmentTemplate, so the first one found is the one we want.
    QString initUrl, mediaTpl;
    int startNumber = 1;
    int segmentCount = 0;

    QXmlStreamReader xml(mpdXml);
    bool inTemplate = false;
    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("SegmentTemplate")) {
            if (inTemplate) continue;          // ignore any later representation
            const auto a = xml.attributes();
            initUrl     = a.value(QLatin1String("initialization")).toString();
            mediaTpl    = a.value(QLatin1String("media")).toString();
            startNumber = a.value(QLatin1String("startNumber")).toInt();
            if (startNumber <= 0) startNumber = 1;
            inTemplate = true;
        } else if (inTemplate && xml.isStartElement() && xml.name() == QLatin1String("S")) {
            // <S d="…" r="n"/> covers n+1 segments; r is absent for a single one
            // and negative only in live manifests, which Tidal never sends.
            const int r = xml.attributes().value(QLatin1String("r")).toInt();
            segmentCount += 1 + qMax(0, r);
        } else if (inTemplate && xml.isEndElement()
                   && xml.name() == QLatin1String("SegmentTemplate")) {
            break;
        }
    }

    if (initUrl.isEmpty() || mediaTpl.isEmpty() || segmentCount <= 0) return {};

    QStringList urls;
    urls.reserve(segmentCount + 1);
    urls << initUrl;
    for (int n = startNumber; n < startNumber + segmentCount; ++n)
        urls << QString(mediaTpl).replace(QLatin1String("$Number$"), QString::number(n));
    return urls;
}

DashFetcher::DashFetcher(TidalClient *client, const QString &mpdXml, QObject *parent)
    : QObject(parent), m_client(client)
{
    m_urls = parseSegmentUrls(mpdXml);
    m_parts.resize(m_urls.size());
}

DashFetcher::~DashFetcher() {
    abort();
}

void DashFetcher::start() {
    if (m_urls.isEmpty()) {
        emit finished(nullptr, tr("The stream manifest had no audio segments in it."));
        return;
    }
    qInfo() << "[dash] fetching" << m_urls.size() << "segments";
    for (int i = 0; i < kMaxParallel && m_nextToRequest < m_urls.size(); ++i)
        fetchAt(m_nextToRequest++);
}

void DashFetcher::abort() {
    m_aborted = true;
    const auto replies = m_inFlight;
    m_inFlight.clear();
    for (const auto &r : replies)
        if (!r.isNull()) r->abort();
}

void DashFetcher::fetchAt(int index) {
    QNetworkReply *reply = m_client->fetchRaw(QUrl(m_urls.at(index)),
        [this, index](QByteArray data, QString err) {
            if (m_aborted || m_failed) return;

            if (!err.isEmpty() || data.isEmpty()) {
                m_failed = true;
                abort();
                m_aborted = false;   // abort() only cancelled peers, not this signal
                emit finished(nullptr, err.isEmpty()
                    ? tr("A piece of the audio stream came back empty.") : err);
                return;
            }

            m_parts[index] = data;
            ++m_completed;

            if (m_nextToRequest < m_urls.size())
                fetchAt(m_nextToRequest++);
            maybeComplete();
        });

    // fetchRaw may already have run its callback (cached/failed) and cleared
    // state, so only track a reply that is still live.
    if (reply && !m_failed && !m_aborted) m_inFlight.append(reply);
}

void DashFetcher::maybeComplete() {
    if (m_completed < m_urls.size()) return;

    auto *file = new QTemporaryFile(QDir::tempPath()
                                    + QStringLiteral("/tidal-wave-XXXXXX.mp4"));
    file->setAutoRemove(false);
    if (!file->open()) {
        delete file;
        emit finished(nullptr, tr("Failed to write the temporary audio file."));
        return;
    }
    for (const QByteArray &part : std::as_const(m_parts))
        file->write(part);
    file->flush();
    file->close();

    m_parts.clear();   // a full track is tens of megabytes; drop it promptly
    qInfo() << "[dash] joined" << m_urls.size() << "segments ->" << file->fileName()
            << QFileInfo(file->fileName()).size() << "bytes";
    emit finished(file, QString());
}
