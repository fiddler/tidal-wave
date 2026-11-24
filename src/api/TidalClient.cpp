#include <tuple>
#include "TidalClient.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QByteArray>
#include <QDebug>
#include <QSet>
#include <QHash>
#include <memory>

TidalClient::TidalClient(TidalApi *api, QObject *parent)
    : QObject(parent), m_api(api) {}

QString TidalClient::qualityString(AudioQuality q) {
    switch (q) {
        case AudioQuality::Low96k:       return "LOW";
        case AudioQuality::Low320k:      return "HIGH";
        case AudioQuality::Lossless:     return "LOSSLESS";
        case AudioQuality::HiResLossless:return "HI_RES_LOSSLESS";
    }
    return "LOSSLESS";
}

// ─── Parsing helpers ───────────────────────────────

QList<Track> TidalClient::parseTracks(const QJsonObject &root) {
    QList<Track> out;
    QJsonArray items = root.contains("items") ? root["items"].toArray()
                                              : root["data"].toArray();
    for (const auto &v : items) {
        auto obj = v.toObject();
        // Some endpoints wrap tracks in {item: {...}}
        if (obj.contains("item")) obj = obj["item"].toObject();
        if (obj.contains("id"))   out.append(Track::fromJson(obj));
    }
    return out;
}

QList<Album> TidalClient::parseAlbums(const QJsonObject &root) {
    QList<Album> out;
    for (const auto &v : root["items"].toArray()) {
        auto obj = v.toObject();
        if (obj.contains("item")) obj = obj["item"].toObject();
        if (obj.contains("id"))   out.append(Album::fromJson(obj));
    }
    return out;
}

QList<Artist> TidalClient::parseArtists(const QJsonObject &root) {
    QList<Artist> out;
    for (const auto &v : root["items"].toArray()) {
        auto obj = v.toObject();
        if (obj.contains("item")) obj = obj["item"].toObject();
        if (obj.contains("id"))   out.append(Artist::fromJson(obj));
    }
    return out;
}

QList<Playlist> TidalClient::parsePlaylists(const QJsonObject &root) {
    QList<Playlist> out;
    for (const auto &v : root["items"].toArray()) {
        const auto row = v.toObject();
        auto obj = row;
        if (obj.contains("playlist")) obj = obj["playlist"].toObject();
        else if (obj.contains("item")) obj = obj["item"].toObject();
        if (!obj.contains("uuid") && !obj.contains("id")) continue;
        Playlist p = Playlist::fromJson(obj);
        // The wrapping row's date is when the user acquired the playlist.
        const QDateTime added = QDateTime::fromString(row["created"].toString(), Qt::ISODate);
        if (added.isValid() && row.contains("playlist")) p.addedAt = added.toMSecsSinceEpoch();
        out.append(p);
    }
    return out;
}

// ─── Mixes / Home ──────────────────────────────────

QList<Mix> TidalClient::parseMixPage(const QJsonObject &root) {
    QList<Mix> mixes;
    for (const auto &row : root["rows"].toArray()) {
        for (const auto &module : row.toObject()["modules"].toArray()) {
            auto mod = module.toObject();
            if (mod["type"].toString() != "MIX_LIST") continue;
            for (const auto &item : mod["pagedList"].toObject()["items"].toArray()) {
                const Mix mix = Mix::fromJson(item.toObject());
                if (mix.isVideoMix()) continue;
                mixes.append(mix);
            }
        }
    }
    return mixes;
}

Mix TidalClient::parseMixHeader(const QJsonObject &root) {
    for (const auto &row : root["rows"].toArray())
        for (const auto &module : row.toObject()["modules"].toArray()) {
            const QJsonObject mod = module.toObject();
            if (mod["type"].toString() != "MIX_HEADER") continue;
            const Mix mix = Mix::fromJson(mod["mix"].toObject());
            if (!mix.id.isEmpty() || !mix.title.isEmpty()) return mix;
        }
    return {};
}

QList<Track> TidalClient::parseMixTracks(const QJsonObject &root) {
    QList<Track> tracks;
    for (const auto &row : root["rows"].toArray())
        for (const auto &module : row.toObject()["modules"].toArray()) {
            const QJsonObject mod = module.toObject();
            if (mod["type"].toString() != "TRACK_LIST") continue;
            tracks.append(parseTracks(mod["pagedList"].toObject()));
        }
    return tracks;
}

QList<Mix> TidalClient::parseSavedMixes(const QJsonObject &root) {
    // Nothing is filtered: a saved video mix or radio station is something the
    // user put there, and silently dropping it is the worse failure.
    QList<Mix> mixes;
    for (const auto &v : root["items"].toArray()) {
        QJsonObject obj = v.toObject();
        if (obj.contains("item")) obj = obj["item"].toObject();
        const Mix mix = Mix::fromJson(obj);
        if (!mix.id.isEmpty()) mixes.append(mix);
    }
    return mixes;
}

QString TidalClient::nextCursor(const QJsonObject &page, const QString &previous) {
    const QString cursor = page["cursor"].toString();
    if (page["items"].toArray().isEmpty()) return {};
    if (cursor == previous) return {};
    return cursor;
}

QList<Mix> TidalClient::mergeMixLists(const QList<QList<Mix>> &lists) {
    QList<Mix> out;
    QHash<QString, qsizetype> seen;
    for (const QList<Mix> &list : lists) {
        for (const Mix &m : list) {
            if (m.id.isEmpty()) { out.append(m); continue; }
            const auto at = seen.constFind(m.id);
            if (at == seen.constEnd()) {
                seen.insert(m.id, out.size());
                out.append(m);
                continue;
            }
            // The saved record is the only one that knows when it was saved.
            Mix &kept = out[*at];
            if (kept.addedAt == 0 && m.addedAt > 0) kept.addedAt = m.addedAt;
        }
    }
    return out;
}

void TidalClient::fetchGeneratedMixes(MixesCallback cb) {
    QUrlQuery q;
    q.addQueryItem("deviceType", "BROWSER");
    m_api->get(QStringLiteral("pages/my_collection_my_mixes"), q,
        [cb](QJsonObject root, QString err) {
            if (!err.isEmpty()) { cb({}, err); return; }
            cb(parseMixPage(root), {});
        });
}

void TidalClient::fetchSavedMixes(MixesCallback cb, const QString &cursor,
                                  QList<Mix> acc, int page)
{
    QUrlQuery q;
    q.addQueryItem("limit", QString::number(kSavedMixesPageSize));
    if (!cursor.isEmpty()) q.addQueryItem("cursor", cursor);
    m_api->getV2(QStringLiteral("favorites/mixes"), q,
        [this, cb, cursor, acc, page](QJsonObject root, QString err) mutable {
            if (!err.isEmpty()) {
                // A later page failing still delivers the earlier ones.
                cb(acc, acc.isEmpty() ? err : QString());
                return;
            }
            acc.append(parseSavedMixes(root));
            const QString next = nextCursor(root, cursor);
            if (next.isEmpty() || page + 1 >= kMaxSavedMixesPages) { cb(acc, {}); return; }
            fetchSavedMixes(cb, next, acc, page + 1);
        });
}

void TidalClient::fetchHomeMixes(MixesCallback cb) {
    struct Pending {
        QList<Mix> generated;
        QList<Mix> saved;
        int        outstanding = 2;
        QString    err;
    };
    const auto pending = std::make_shared<Pending>();

    const auto finish = [cb, pending]() {
        if (--pending->outstanding > 0) return;
        // Generated first: it holds "My Mix 1".."My Mix 8" as a run.
        const QList<Mix> mixes = mergeMixLists({pending->generated, pending->saved});
        // One source failing must not empty the list.
        if (mixes.isEmpty() && !pending->err.isEmpty()) { cb({}, pending->err); return; }
        cb(mixes, {});
    };

    fetchGeneratedMixes([pending, finish](QList<Mix> mixes, QString err) {
        if (err.isEmpty()) pending->generated = mixes;
        else if (pending->err.isEmpty()) pending->err = err;
        finish();
    });
    fetchSavedMixes([pending, finish](QList<Mix> mixes, QString err) {
        if (err.isEmpty()) pending->saved = mixes;
        else if (pending->err.isEmpty()) pending->err = err;
        finish();
    });
}

void TidalClient::fetchMixPage(const QString &mixId, MixPageCallback cb) {
    QUrlQuery q;
    q.addQueryItem("mixId", mixId);
    q.addQueryItem("deviceType", "BROWSER");
    m_api->get(QStringLiteral("pages/mix"), q, [cb](QJsonObject root, QString err) {
        if (!err.isEmpty()) { cb({}, {}, err); return; }
        cb(parseMixHeader(root), parseMixTracks(root), {});
    });
}

void TidalClient::fetchMixTracks(const QString &mixId, TracksCallback cb) {
    fetchMixPage(mixId, [cb](Mix, QList<Track> tracks, QString err) { cb(tracks, err); });
}

bool TidalClient::mixFavoriteAccepted(const QJsonObject &reply, const QString &mixId,
                                      bool adding)
{
    if (mixId.isEmpty()) return false;
    const QJsonArray items = reply.value(adding ? QLatin1String("addedItems")
                                               : QLatin1String("deletedItems")).toArray();
    for (const auto &v : items)
        if (v.toString() == mixId) return true;
    return false;
}

void TidalClient::addMixFavorite(const QString &mixId, std::function<void(bool)> cb) {
    if (mixId.isEmpty()) { cb(false); return; }
    QUrlQuery q;
    q.addQueryItem("mixIds", mixId);
    q.addQueryItem("onArtifactNotFound", "FAIL");
    m_api->putV2(QStringLiteral("favorites/mixes/add"), q,
        [cb, mixId](QJsonObject reply, QString err) {
            cb(err.isEmpty() && mixFavoriteAccepted(reply, mixId, true));
        });
}

void TidalClient::removeMixFavorite(const QString &mixId, std::function<void(bool)> cb) {
    if (mixId.isEmpty()) { cb(false); return; }
    QUrlQuery q;
    q.addQueryItem("mixIds", mixId);
    q.addQueryItem("onArtifactNotFound", "FAIL");
    m_api->putV2(QStringLiteral("favorites/mixes/remove"), q,
        [cb, mixId](QJsonObject reply, QString err) {
            cb(err.isEmpty() && mixFavoriteAccepted(reply, mixId, false));
        });
}

// ─── Favorites ─────────────────────────────────────

void TidalClient::fetchFavoriteTracks(TracksCallback cb, int limit, int offset) {
    QUrlQuery q;
    q.addQueryItem("limit",  QString::number(limit));
    q.addQueryItem("offset", QString::number(offset));
    q.addQueryItem("order",  "DATE");
    m_api->get(QStringLiteral("users/%1/favorites/tracks").arg(m_userId), q,
        [this, cb](QJsonObject root, QString err) {
            cb(err.isEmpty() ? parseTracks(root) : QList<Track>{}, err); });
}

// Albums and artists: newest first. Without an order the endpoint answers by
// name, so a just-saved album landed somewhere in the middle. The date sits on
// the favourites row, not on the item, so the ordering has to come from the
// server. If it refuses the parameters, ask again without them rather than
// letting the collection come back empty.
void TidalClient::fetchFavoriteAlbums(AlbumsCallback cb, int limit, int offset) {
    const QString path = QStringLiteral("users/%1/favorites/albums").arg(m_userId);
    QUrlQuery plain;
    plain.addQueryItem("limit",  QString::number(limit));
    plain.addQueryItem("offset", QString::number(offset));
    // One order per session: a later page in another order is a slice of a
    // different list, so only a first page may fall back, and it sticks.
    if (m_plainAlbumOrder) {
        m_api->get(path, plain, [cb](QJsonObject root, QString err) {
            cb(err.isEmpty() ? parseAlbums(root) : QList<Album>{}, err); });
        return;
    }
    QUrlQuery q = plain;
    q.addQueryItem("order",  "DATE");
    q.addQueryItem("orderDirection", "DESC");
    m_api->get(path, q, [this, cb, path, plain, offset](QJsonObject root, QString err) {
        if (err.isEmpty()) { cb(parseAlbums(root), {}); return; }
        if (offset > 0) { cb({}, err); return; }
        m_api->get(path, plain, [this, cb](QJsonObject root2, QString err2) {
            if (err2.isEmpty()) m_plainAlbumOrder = true;
            cb(err2.isEmpty() ? parseAlbums(root2) : QList<Album>{}, err2); });
    });
}

void TidalClient::fetchFavoriteArtists(ArtistsCallback cb, int limit, int offset) {
    const QString path = QStringLiteral("users/%1/favorites/artists").arg(m_userId);
    QUrlQuery plain;
    plain.addQueryItem("limit",  QString::number(limit));
    plain.addQueryItem("offset", QString::number(offset));
    // One order per session: a later page in another order is a slice of a
    // different list, so only a first page may fall back, and it sticks.
    if (m_plainArtistOrder) {
        m_api->get(path, plain, [cb](QJsonObject root, QString err) {
            cb(err.isEmpty() ? parseArtists(root) : QList<Artist>{}, err); });
        return;
    }
    QUrlQuery q = plain;
    q.addQueryItem("order",  "DATE");
    q.addQueryItem("orderDirection", "DESC");
    m_api->get(path, q, [this, cb, path, plain, offset](QJsonObject root, QString err) {
        if (err.isEmpty()) { cb(parseArtists(root), {}); return; }
        if (offset > 0) { cb({}, err); return; }
        m_api->get(path, plain, [this, cb](QJsonObject root2, QString err2) {
            if (err2.isEmpty()) m_plainArtistOrder = true;
            cb(err2.isEmpty() ? parseArtists(root2) : QList<Artist>{}, err2); });
    });
}

void TidalClient::fetchUserPlaylists(PlaylistsCallback cb, int limit, int offset) {
    QUrlQuery q;
    q.addQueryItem("limit", QString::number(limit));
    q.addQueryItem("offset", QString::number(offset));
    m_api->get(QStringLiteral("users/%1/playlistsAndFavoritePlaylists").arg(m_userId), q,
        [this, cb](QJsonObject root, QString err) {
            cb(err.isEmpty() ? parsePlaylists(root) : QList<Playlist>{}, err); });
}

// ─── Content ───────────────────────────────────────

void TidalClient::fetchAlbumTracks(qint64 albumId, TracksCallback cb) {
    fetchAllTracks(QStringLiteral("albums/%1/tracks").arg(albumId), cb);
}

void TidalClient::fetchPlaylistTracks(const QString &uuid, TracksCallback cb) {
    fetchAllTracks(QStringLiteral("playlists/%1/tracks").arg(uuid), cb);
}

void TidalClient::fetchAllTracks(const QString &endpoint, TracksCallback cb,
                                 int offset, QList<Track> acc)
{
    QUrlQuery q;
    q.addQueryItem("limit", "100");
    q.addQueryItem("offset", QString::number(offset));
    m_api->get(endpoint, q,
        [this, endpoint, cb, offset, acc](QJsonObject root, QString err) mutable {
            if (!err.isEmpty()) {
                cb(acc, acc.isEmpty() ? err : QString());
                return;
            }
            QList<Track> page = parseTracks(root);
            acc.append(page);
            int total = root["totalNumberOfItems"].toInt(acc.size());
            if (page.isEmpty() || acc.size() >= total)
                cb(acc, {});
            else
                fetchAllTracks(endpoint, cb, offset + page.size(), acc);
        });
}

void TidalClient::fetchAlbum(qint64 albumId, std::function<void(Album,QString)> cb) {
    m_api->get(QStringLiteral("albums/%1").arg(albumId), {},
        [cb](QJsonObject root, QString err) {
            cb(err.isEmpty() ? Album::fromJson(root) : Album{}, err); });
}

void TidalClient::fetchAlbumStatus(qint64 albumId, std::function<void(Album,QString,int)> cb) {
    m_api->getStatus(QStringLiteral("albums/%1").arg(albumId), {},
        [cb](QJsonObject root, QString err, int status) {
            cb(err.isEmpty() ? Album::fromJson(root) : Album{}, err, status); });
}

void TidalClient::fetchArtist(qint64 artistId, std::function<void(Artist,QString)> cb) {
    m_api->get(QStringLiteral("artists/%1").arg(artistId), {},
        [cb](QJsonObject root, QString err) {
            cb(err.isEmpty() ? Artist::fromJson(root) : Artist{}, err); });
}

void TidalClient::fetchTrack(qint64 trackId, std::function<void(Track,QString)> cb) {
    m_api->get(QStringLiteral("tracks/%1").arg(trackId), {},
        [cb](QJsonObject root, QString err) {
            cb(err.isEmpty() ? Track::fromJson(root) : Track{}, err); });
}

void TidalClient::fetchArtistDetail(qint64 artistId,
    std::function<void(ArtistDetail, QString)> cb)
{
    m_api->get(QStringLiteral("artists/%1").arg(artistId), {},
        [this, artistId, cb](QJsonObject root, QString err) {
            if (!err.isEmpty()) { cb({}, err); return; }
            ArtistDetail d;
            d.id      = root["id"].toVariant().toLongLong();
            d.name    = root["name"].toString();
            d.picture = root["picture"].toString();
            // Bio comes from a separate endpoint
            m_api->get(QStringLiteral("artists/%1/bio").arg(artistId), {},
                [d, cb](QJsonObject bio, QString) mutable {
                    d.bio = bio["text"].toString();
                    cb(d, {});
                });
        });
}

QList<Album> TidalClient::mergeAlbumLists(const QList<QList<Album>> &lists) {
    QList<Album> out;
    QSet<qint64> seen;
    for (const QList<Album> &list : lists) {
        for (const Album &a : list) {
            if (a.id != 0) {
                if (seen.contains(a.id)) continue;
                seen.insert(a.id);
            }
            out.append(a);
        }
    }
    return out;
}

void TidalClient::fetchArtistAlbumPage(qint64 artistId, const QString &filter,
                                       AlbumsCallback cb, int offset, QList<Album> acc)
{
    QUrlQuery q;
    if (!filter.isEmpty()) q.addQueryItem("filter", filter);
    q.addQueryItem("limit", QString::number(kArtistAlbumsPageSize));
    q.addQueryItem("offset", QString::number(offset));
    m_api->get(QStringLiteral("artists/%1/albums").arg(artistId), q,
        [this, artistId, filter, cb, offset, acc](QJsonObject root, QString err) mutable {
            if (!err.isEmpty()) {
                cb(acc, acc.isEmpty() ? err : QString());
                return;
            }
            const QList<Album> page = parseAlbums(root);
            acc.append(page);
            const int total = root["totalNumberOfItems"].toInt(int(acc.size()));
            if (page.isEmpty() || acc.size() >= total || acc.size() >= kMaxArtistAlbums) {
                cb(acc, {});
                return;
            }
            fetchArtistAlbumPage(artistId, filter, cb, offset + int(page.size()), acc);
        });
}

// One discography, three requests. Unfiltered, artists/<id>/albums answers
// albums only: singles, EPs and compilations each need their own filter, and
// the three id sets are disjoint. Merged here so the page gets one reply.
void TidalClient::fetchArtistAlbums(qint64 artistId, AlbumsCallback cb) {
    struct Pending {
        QList<Album> lists[3];
        int          outstanding = 3;
        QString      err;
    };
    const auto pending = std::make_shared<Pending>();
    static const char *const filters[3] = { "", "EPSANDSINGLES", "COMPILATIONS" };

    for (int i = 0; i < 3; ++i) {
        fetchArtistAlbumPage(artistId, QString::fromLatin1(filters[i]),
            [cb, pending, i](QList<Album> albums, QString err) {
                if (err.isEmpty()) pending->lists[i] = albums;
                else if (pending->err.isEmpty()) pending->err = err;
                if (--pending->outstanding > 0) return;
                const QList<Album> all = mergeAlbumLists(
                    {pending->lists[0], pending->lists[1], pending->lists[2]});
                // One filter failing must not empty the other two.
                if (all.isEmpty() && !pending->err.isEmpty()) { cb({}, pending->err); return; }
                cb(all, {});
            });
    }
}

void TidalClient::fetchArtistTopTracks(qint64 artistId, TracksCallback cb) {
    QUrlQuery q;
    q.addQueryItem("limit", "10");
    m_api->get(QStringLiteral("artists/%1/toptracks").arg(artistId), q,
        [this, cb](QJsonObject root, QString err) {
            cb(err.isEmpty() ? parseTracks(root) : QList<Track>{}, err); });
}

// ─── Search ────────────────────────────────────────

SearchResults TidalClient::parseSearch(const QJsonObject &root) {
    SearchResults r;
    r.tracks         = parseTracks(root["tracks"].toObject());
    r.totalTracks    = root["tracks"].toObject()["totalNumberOfItems"].toInt();
    r.albums         = parseAlbums(root["albums"].toObject());
    r.totalAlbums    = root["albums"].toObject()["totalNumberOfItems"].toInt();
    r.artists        = parseArtists(root["artists"].toObject());
    r.totalArtists   = root["artists"].toObject()["totalNumberOfItems"].toInt();
    r.playlists      = parsePlaylists(root["playlists"].toObject());
    r.totalPlaylists = root["playlists"].toObject()["totalNumberOfItems"].toInt();
    for (const auto &v : root["mixes"].toObject()["items"].toArray()) {
        QJsonObject item = v.toObject();
        if (item.contains("item")) item = item["item"].toObject();
        const Mix mix = Mix::fromJson(item);
        if (mix.id.isEmpty() || mix.isVideoMix()) continue;
        r.mixes.append(mix);
    }
    r.totalMixes     = root["mixes"].toObject()["totalNumberOfItems"].toInt();
    return r;
}

void TidalClient::search(const QString &query, SearchCb cb, int limit, int offset,
                         const QString &types) {
    QUrlQuery q;
    q.addQueryItem("query", query);
    q.addQueryItem("limit", QString::number(limit));
    q.addQueryItem("offset", QString::number(offset));
    q.addQueryItem("types", types.isEmpty()
                   ? QStringLiteral("TRACKS,ALBUMS,ARTISTS,PLAYLISTS,MIXES") : types);
    m_api->get("search", q, [cb](QJsonObject root, QString err) {
        if (!err.isEmpty()) { cb({}, err); return; }
        cb(parseSearch(root), {});
    });
}

// ─── Favorites management ──────────────────────────

void TidalClient::addTrackFavorite(qint64 trackId, std::function<void(bool)> cb) {
    QUrlQuery form;
    form.addQueryItem("trackIds", QString::number(trackId));
    form.addQueryItem("countryCode", m_api->countryCode());
    m_api->postApiForm(QStringLiteral("users/%1/favorites/tracks").arg(m_userId), form,
        [cb](QJsonObject, QString err) { cb(err.isEmpty()); });
}

void TidalClient::removeTrackFavorite(qint64 trackId, std::function<void(bool)> cb) {
    QUrlQuery params;
    params.addQueryItem("countryCode", m_api->countryCode());
    m_api->deleteApi(QStringLiteral("users/%1/favorites/tracks/%2").arg(m_userId).arg(trackId), params,
        [cb](QJsonObject, QString err) { cb(err.isEmpty()); });
}

void TidalClient::addAlbumFavorite(qint64 albumId, std::function<void(bool)> cb) {
    QUrlQuery form;
    form.addQueryItem("albumIds", QString::number(albumId));
    form.addQueryItem("countryCode", m_api->countryCode());
    m_api->postApiForm(QStringLiteral("users/%1/favorites/albums").arg(m_userId), form,
        [cb](QJsonObject, QString err) { cb(err.isEmpty()); });
}

void TidalClient::removeAlbumFavorite(qint64 albumId, std::function<void(bool)> cb) {
    QUrlQuery params;
    params.addQueryItem("countryCode", m_api->countryCode());
    m_api->deleteApi(QStringLiteral("users/%1/favorites/albums/%2").arg(m_userId).arg(albumId), params,
        [cb](QJsonObject, QString err) { cb(err.isEmpty()); });
}

void TidalClient::addArtistFavorite(qint64 artistId, std::function<void(bool)> cb) {
    QUrlQuery form;
    form.addQueryItem("artistIds", QString::number(artistId));
    form.addQueryItem("countryCode", m_api->countryCode());
    m_api->postApiForm(QStringLiteral("users/%1/favorites/artists").arg(m_userId), form,
        [cb](QJsonObject, QString err) { cb(err.isEmpty()); });
}

void TidalClient::removeArtistFavorite(qint64 artistId, std::function<void(bool)> cb) {
    QUrlQuery params;
    params.addQueryItem("countryCode", m_api->countryCode());
    m_api->deleteApi(QStringLiteral("users/%1/favorites/artists/%2").arg(m_userId).arg(artistId), params,
        [cb](QJsonObject, QString err) { cb(err.isEmpty()); });
}

// ─── Streaming ─────────────────────────────────────

void TidalClient::fetchStreamManifest(qint64 trackId, StreamCb cb) {
    fetchStreamManifest(trackId, m_quality, std::move(cb));
}

void TidalClient::fetchStreamManifest(qint64 trackId, AudioQuality quality, StreamCb cb) {
    QUrlQuery q;
    q.addQueryItem("playbackmode",      "STREAM");
    q.addQueryItem("assetpresentation", "FULL");
    q.addQueryItem("audioquality",      qualityString(quality));
    q.addQueryItem("prefetch",          "false");

    m_api->getStatus(QStringLiteral("tracks/%1/playbackinfopostpaywall").arg(trackId), q,
        [cb](QJsonObject root, QString err, int) {
            if (!err.isEmpty()) {
                StreamManifest refused;
                refused.refusedSubStatus = root["subStatus"].toInt();
                cb(refused, err);
                return;
            }

            StreamManifest m;
            m.codec      = root["audioQuality"].toString();
            m.sampleRate = root["sampleRate"].toInt(44100);
            m.bitDepth   = root["bitDepth"].toInt(16);
            m.replayGainTrack = root["trackReplayGain"].toDouble();
            m.replayGainAlbum = root["albumReplayGain"].toDouble();

            QString mimeType = root["manifestMimeType"].toString();
            QByteArray manifestB64 = root["manifest"].toString().toUtf8();
            QByteArray manifestData = QByteArray::fromBase64(manifestB64);

            if (mimeType == "application/vnd.tidal.bts") {
                // BTS format: JSON with URL array
                auto bts = QJsonDocument::fromJson(manifestData).object();
                auto urls = bts["urls"].toArray();
                if (!urls.isEmpty()) {
                    m.type     = StreamManifest::BTS;
                    m.url      = urls[0].toString();
                    m.mimeType = bts["mimeType"].toString();
                }
            } else {
                // MPD format: MPEG-DASH manifest XML
                m.type     = StreamManifest::MPD;
                m.url      = QString::fromUtf8(manifestData);
                m.mimeType = "application/dash+xml";
            }
            cb(m, {});
        });
}

QNetworkReply* TidalClient::fetchRaw(const QUrl &url, std::function<void(QByteArray, QString)> cb) {
    return m_api->getRaw(url, cb);
}

// ─── Playlist management ───────────────────────────

void TidalClient::createPlaylist(const QString &title,
    std::function<void(Playlist, QString)> cb)
{
    QUrlQuery form;
    // Pre-encoded: a raw "R&B" would end the field at the ampersand.
    form.addQueryItem("title",       QString::fromUtf8(QUrl::toPercentEncoding(title)));
    form.addQueryItem("description", "");
    m_api->postApiForm(QStringLiteral("users/%1/playlists").arg(m_userId), form,
        [cb](QJsonObject root, QString err) {
            if (!err.isEmpty()) { cb({}, err); return; }
            cb(Playlist::fromJson(root), {});
        });
}

// One playlist's own metadata, track count included. Add and remove answer
// without it, so callers that cache a playlist ask for it again afterwards.
void TidalClient::fetchPlaylist(const QString &uuid,
    std::function<void(Playlist, QString)> cb)
{
    m_api->get(QStringLiteral("playlists/%1").arg(uuid), {},
        [cb](QJsonObject root, QString err) {
            if (!err.isEmpty()) { cb({}, err); return; }
            cb(Playlist::fromJson(root), {});
        });
}

// Both fields go every time: the endpoint replaces what it is sent rather than
// merging, so posting the title alone would clear the description.
void TidalClient::editPlaylist(const QString &uuid, const QString &title,
    const QString &description, std::function<void(bool)> cb)
{
    m_api->getEtag(QStringLiteral("playlists/%1").arg(uuid),
        [this, uuid, title, description, cb](QString etag, QString err) {
            if (!err.isEmpty()) { cb(false); return; }
            QUrlQuery form;
            form.addQueryItem("title",       QString::fromUtf8(QUrl::toPercentEncoding(title)));
            form.addQueryItem("description", QString::fromUtf8(QUrl::toPercentEncoding(description)));
            m_api->postApiFormEtag(QStringLiteral("playlists/%1").arg(uuid), form, etag,
                [cb](QJsonObject, QString e) { cb(e.isEmpty()); });
        });
}

// Tidal's endpoint takes a comma-separated trackIds list, so adding many
// tracks costs the same single request as adding one.
void TidalClient::addTracksToPlaylist(const QString &uuid, const QList<qint64> &trackIds,
    std::function<void(bool, QString)> cb)
{
    if (trackIds.isEmpty()) { cb(false, {}); return; }
    m_api->getEtag(QStringLiteral("playlists/%1").arg(uuid),
        [this, uuid, trackIds, cb](QString etag, QString err) {
            if (!err.isEmpty()) { cb(false, err); return; }
            QStringList ids;
            for (qint64 id : trackIds) ids << QString::number(id);
            QUrlQuery form;
            form.addQueryItem("trackIds",           ids.join(QLatin1Char(',')));
            form.addQueryItem("onArtifactNotFound", "FAIL");
            form.addQueryItem("onDuplicateFound",   "SKIP");
            m_api->postApiFormEtag(QStringLiteral("playlists/%1/items").arg(uuid), form, etag,
                [cb](QJsonObject, QString e) { cb(e.isEmpty(), e); });
        });
}

// Answers 204 and needs no ETag (probed 2026-11-24).
void TidalClient::deletePlaylist(const QString &uuid, std::function<void(bool, QString)> cb) {
    m_api->deleteApi(QStringLiteral("playlists/%1").arg(uuid), {},
        [cb](QJsonObject, QString e) { cb(e.isEmpty(), e); });
}

// Reordering posts to the item's own index with the destination in the form,
// guarded by the playlist ETag exactly like add and remove.
void TidalClient::moveTrackInPlaylist(const QString &uuid, int fromIndex, int toIndex,
    std::function<void(bool)> cb)
{
    if (fromIndex == toIndex || fromIndex < 0 || toIndex < 0) { cb(false); return; }
    m_api->getEtag(QStringLiteral("playlists/%1").arg(uuid),
        [this, uuid, fromIndex, toIndex, cb](QString etag, QString err) {
            if (!err.isEmpty()) { cb(false); return; }
            QUrlQuery form;
            form.addQueryItem("toIndex", QString::number(toIndex));
            m_api->postApiFormEtag(QStringLiteral("playlists/%1/items/%2").arg(uuid).arg(fromIndex),
                form, etag, [cb](QJsonObject, QString e) { cb(e.isEmpty()); });
        });
}

void TidalClient::removeTrackFromPlaylist(const QString &uuid, int itemIndex,
    std::function<void(bool)> cb)
{
    m_api->getEtag(QStringLiteral("playlists/%1").arg(uuid),
        [this, uuid, itemIndex, cb](QString etag, QString err) {
            if (!err.isEmpty()) { cb(false); return; }
            QUrlQuery params;
            params.addQueryItem("toIndex",   QString::number(itemIndex));
            params.addQueryItem("fromIndex", QString::number(itemIndex));
            params.addQueryItem("order",     "INDEX");
            m_api->deleteApiEtag(QStringLiteral("playlists/%1/items").arg(uuid), params, etag,
                [cb](QJsonObject, QString e) { cb(e.isEmpty()); });
        });
}

void TidalClient::fetchTrackRadio(qint64 trackId, TracksCallback cb) {
    QUrlQuery q;
    q.addQueryItem("limit", "50");
    m_api->get(QStringLiteral("tracks/%1/radio").arg(trackId), q,
        [this, cb](QJsonObject root, QString err) {
            cb(err.isEmpty() ? parseTracks(root) : QList<Track>{}, err);
        });
}

void TidalClient::fetchTwin(const Track &of, std::function<void(Track, QString)> cb) {
    if (of.isrc.isEmpty()) { cb({}, {}); return; }
    QUrlQuery q;
    q.addQueryItem("isrc", of.isrc);
    // Answers the tracks of that ISRC that are streamed in this country
    // (probed 2026-11-17: 9 of 12 dead playlist rows had one).
    m_api->get(QStringLiteral("tracks"), q,
        [of, cb](QJsonObject root, QString err) {
            cb(err.isEmpty() ? pickTwin(root, of) : Track{}, err);
        });
}

Track TidalClient::pickTwin(const QJsonObject &root, const Track &of) {
    // Lower is better, field by field: the same album title first, then
    // hi-res, then the more popular, then the lower id.
    using Rank = std::tuple<bool, bool, int, qint64>;
    Track best;
    Rank  bestRank;
    for (const auto &v : root["items"].toArray()) {
        const QJsonObject j = v.toObject();
        const Track t = Track::fromJson(j);
        if (t.id <= 0 || t.id == of.id || !t.available) continue;
        if (t.isrc.compare(of.isrc, Qt::CaseInsensitive) != 0) continue;
        const bool hiRes = j["mediaMetadata"].toObject()["tags"].toArray()
                               .contains(QStringLiteral("HIRES_LOSSLESS"));
        const Rank rank{t.album.title != of.album.title, !hiRes, -t.popularity, t.id};
        if (best.id == 0 || rank < bestRank) { best = t; bestRank = rank; }
    }
    return best;
}

void TidalClient::fetchLyrics(qint64 trackId, std::function<void(QString, bool, QString)> cb) {
    m_api->get(QStringLiteral("tracks/%1/lyrics").arg(trackId), {},
        [cb](QJsonObject root, QString err) {
            if (!err.isEmpty()) { cb({}, false, err); return; }
            QString text = root["subtitles"].toString();
            bool timed = !text.isEmpty();
            if (text.isEmpty()) text = root["text"].toString();
            cb(text, timed, {});
        });
}

QList<CreditGroup> TidalClient::parseCredits(const QJsonArray &groups) {
    QList<CreditGroup> out;
    for (const auto &v : groups) {
        const CreditGroup g = TrackCredits::groupFromJson(v.toObject());
        if (!g.type.isEmpty() && !g.contributors.isEmpty()) out.append(g);
    }
    return out;
}

// The contributor list is what was asked for, so only its failure is an error.
// The track and album requests behind it add the rights line and release date;
// if they fail those fields stay empty and the credits still arrive.
void TidalClient::fetchTrackCredits(qint64 trackId,
    std::function<void(TrackCredits, QString)> cb)
{
    QUrlQuery q;
    q.addQueryItem("includeContributors", "true");
    m_api->getArray(QStringLiteral("tracks/%1/credits").arg(trackId), q,
        [this, trackId, cb](QJsonArray groups, QString err) {
            if (!err.isEmpty()) { cb({}, err); return; }
            auto out = std::make_shared<TrackCredits>();
            out->groups = parseCredits(groups);

            m_api->get(QStringLiteral("tracks/%1").arg(trackId), {},
                [this, out, cb](QJsonObject track, QString trackErr) {
                    qint64 albumId = 0;
                    if (trackErr.isEmpty()) {
                        out->copyright = track["copyright"].toString();
                        out->isrc      = track["isrc"].toString();
                        albumId = track["album"].toObject()["id"].toVariant().toLongLong();
                    }
                    if (albumId <= 0) { cb(*out, {}); return; }
                    m_api->get(QStringLiteral("albums/%1").arg(albumId), {},
                        [out, cb](QJsonObject album, QString albumErr) {
                            if (albumErr.isEmpty()) {
                                out->releaseDate = album["releaseDate"].toString();
                                out->upc         = album["upc"].toString();
                                if (out->copyright.isEmpty())
                                    out->copyright = album["copyright"].toString();
                            }
                            cb(*out, {});
                        });
                });
        });
}

void TidalClient::fetchRecentlyPlayed(TracksCallback cb) {
    QUrlQuery q;
    q.addQueryItem("limit", "20");
    m_api->get(QStringLiteral("users/%1/history").arg(m_userId), q,
        [this, cb](QJsonObject root, QString err) {
            if (!err.isEmpty()) { cb({}, err); return; }
            QList<Track> tracks;
            for (const auto &v : root["items"].toArray()) {
                auto obj  = v.toObject();
                auto item = obj["item"].toObject();
                if (item.contains("id"))
                    tracks.append(Track::fromJson(item));
            }
            cb(tracks, {});
        });
}
