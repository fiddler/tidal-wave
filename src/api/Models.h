#pragma once
#include <QString>
#include <QList>
#include <QUrl>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

namespace Tidal {

enum class AudioQuality { Low96k, Low320k, Lossless, HiResLossless };
enum class MediaType { Track, Album, Artist, Playlist, Mix };

// The mixType values the app treats differently. Matched on, never the title:
// titles arrive in the account's language.
namespace MixTypes {
inline constexpr auto VideoDailyMix = "VIDEO_DAILY_MIX";
inline constexpr auto TrackMix      = "TRACK_MIX";
}

struct Artist {
    qint64 id = 0;
    QString name;
    QString picture; // UUID for art

    static Artist fromJson(const QJsonObject &j) {
        Artist a;
        a.id   = j["id"].toVariant().toLongLong();
        a.name = j["name"].toString();
        a.picture = j["picture"].toString();
        return a;
    }
};

struct Album {
    qint64  id = 0;
    QString title;
    QString cover;      // UUID for art
    int     numTracks = 0;
    int     duration  = 0;
    QString releaseDate;
    QString audioQuality;
    QString type;       // ALBUM / SINGLE / EP / COMPILATION
    QList<Artist> artists;

    QString coverUrl(int size = 320) const {
        if (cover.isEmpty()) return {};
        QString u = cover;
        u.replace('-', '/');
        return QStringLiteral("https://resources.tidal.com/images/%1/%2x%2.jpg").arg(u).arg(size);
    }

    static Album fromJson(const QJsonObject &j) {
        Album a;
        a.id           = j["id"].toVariant().toLongLong();
        a.title        = j["title"].toString();
        a.cover        = j["cover"].toString();
        a.numTracks    = j["numberOfTracks"].toInt();
        a.duration     = j["duration"].toInt();
        a.releaseDate  = j["releaseDate"].toString();
        a.audioQuality = j["audioQuality"].toString();
        a.type         = j["type"].toString();
        for (const auto &v : j["artists"].toArray())
            a.artists.append(Artist::fromJson(v.toObject()));
        return a;
    }

    QString artistNames() const {
        QStringList names;
        for (const auto &a : artists) names << a.name;
        return names.join(", ");
    }
};

struct Track {
    qint64  id = 0;
    QString title;
    int     duration = 0;
    int     trackNumber = 0;
    int     discNumber  = 1;
    bool    explicit_  = false;
    int     popularity  = 0;
    QString audioQuality;
    // False for tracks Tidal still lists but no longer streams (rights expired
    // or region-locked). Such tracks are shown greyed out and are never queued.
    bool    available = true;
    Album   album;
    QList<Artist> artists;
    // Set only for tracks from the local library. When present the player
    // plays this file directly and never asks Tidal for a stream manifest.
    // Local tracks carry a negative id so they can never collide with a
    // Tidal track id or be sent to a Tidal endpoint by mistake.
    QString localPath;
    // The id of this track's radio station as a mix, from `mixes.TRACK_MIX`.
    // Empty on endpoints that leave `mixes` out; the radio then falls back to
    // the bare tracks/<id>/radio list, which has no identity and cannot be saved.
    QString trackMixId;

    bool isLocal() const { return !localPath.isEmpty(); }

    static Track fromJson(const QJsonObject &j) {
        Track t;
        t.id           = j["id"].toVariant().toLongLong();
        t.title        = j["title"].toString();
        t.duration     = j["duration"].toInt();
        t.trackNumber  = j["trackNumber"].toInt();
        t.discNumber   = j["volumeNumber"].toInt(1);
        t.explicit_    = j["explicit"].toBool();
        t.popularity   = j["popularity"].toInt();
        t.audioQuality = j["audioQuality"].toString();
        t.trackMixId   = j["mixes"].toObject()[MixTypes::TrackMix].toString();
        // Both flags default to true: a few endpoints omit them, and an absent
        // flag must not grey out a track that plays perfectly well.
        t.available    = j["allowStreaming"].toBool(true) && j["streamReady"].toBool(true);
        if (j.contains("album"))
            t.album    = Album::fromJson(j["album"].toObject());
        for (const auto &v : j["artists"].toArray())
            t.artists.append(Artist::fromJson(v.toObject()));
        // Fallback: some endpoints include top-level artistId when artist objects lack id
        if (!t.artists.isEmpty() && t.artists[0].id == 0) {
            qint64 fallback = j["artistId"].toVariant().toLongLong();
            if (fallback > 0) t.artists[0].id = fallback;
        }
        return t;
    }

    QString artistNames() const {
        QStringList names;
        for (const auto &a : artists) names << a.name;
        return names.join(", ");
    }

    QString durationString() const {
        int m = duration / 60;
        int s = duration % 60;
        return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
    }

    QString coverUrl(int size = 320) const {
        return album.coverUrl(size);
    }
};

struct Playlist {
    QString uuid;
    QString title;
    QString description;
    int     numTracks = 0;
    int     duration  = 0;
    QString image;  // UUID
    QString type;   // USER / EDITORIAL
    QString created;      // ISO timestamps as sent — sort lexicographically
    QString lastUpdated;
    qint64  creatorId = 0;   // 0 for editorial; your uid for playlists you made
    // When this user created or saved it, ms since epoch, from the collection
    // row the playlist arrived in. Not `created`: for a saved playlist that is
    // the author's date and can be years old.
    qint64  addedAt = 0;

    QString coverUrl(int size = 320) const {
        if (image.isEmpty()) return {};
        QString u = image;
        u.replace('-', '/');
        return QStringLiteral("https://resources.tidal.com/images/%1/%2x%2.jpg").arg(u).arg(size);
    }

    static Playlist fromJson(const QJsonObject &j) {
        Playlist p;
        p.uuid        = j["uuid"].toString();
        p.title       = j["title"].toString();
        p.description = j["description"].toString();
        p.numTracks   = j["numberOfTracks"].toInt();
        p.duration    = j["duration"].toInt();
        // squareImage is the 1:1 art tidal serves at WxW sizes (what coverUrl
        // requests below); the plain "image" field is a 3:2 widescreen crop
        // that 403s at square dimensions, so prefer squareImage.
        p.image       = j["squareImage"].toString(j["image"].toString());
        p.type        = j["type"].toString();
        p.created     = j["created"].toString();
        p.lastUpdated = j["lastUpdated"].toString();
        p.creatorId   = j["creator"].toObject()["id"].toVariant().toLongLong();
        return p;
    }
};

struct Mix {
    QString id;
    QString title;
    QString subTitle;
    QString cover;  // UUID
    QString mixType;     // DISCOVERY_MIX / TRACK_MIX / ARTIST_MIX / VIDEO_DAILY_MIX …
    // When the user saved it, ms since epoch. Only v2/favorites/mixes says;
    // 0 for a mix that came from a generated feed.
    qint64  addedAt = 0;

    // The app plays no video, and pages/mix answers these with a VIDEO_LIST.
    bool isVideoMix() const { return mixType == QLatin1String(MixTypes::VideoDailyMix); }
    // A track's radio station.
    bool isTrackMix() const { return mixType == QLatin1String(MixTypes::TrackMix); }

    QString coverUrl(int size = 320) const {
        if (cover.isEmpty()) return {};
        if (cover.startsWith(QLatin1String("http"))) return cover;
        QString u = cover;
        u.replace('-', '/');
        return QStringLiteral("https://resources.tidal.com/images/%1/%2x%2.jpg").arg(u).arg(size);
    }

    static Mix fromJson(const QJsonObject &j) {
        Mix m;
        m.id       = j["id"].toString();
        m.title    = j["title"].toString();
        m.subTitle = j["subTitle"].toString();
        m.mixType  = j["mixType"].toString();
        const QDateTime added = QDateTime::fromString(j["dateAdded"].toString(), Qt::ISODate);
        if (added.isValid()) m.addedAt = added.toMSecsSinceEpoch();
        // Try multiple known image locations in the API response
        auto tryImages = [&](const QJsonObject &imgs) {
            if (m.cover.isEmpty() && imgs.contains("LARGE"))
                m.cover = imgs["LARGE"].toObject()["url"].toString();
            if (m.cover.isEmpty() && imgs.contains("MEDIUM"))
                m.cover = imgs["MEDIUM"].toObject()["url"].toString();
            if (m.cover.isEmpty() && imgs.contains("SMALL"))
                m.cover = imgs["SMALL"].toObject()["url"].toString();
        };
        tryImages(j["images"].toObject());
        tryImages(j["detail"].toObject()["images"].toObject());
        tryImages(j["detailImages"].toObject());
        return m;
    }
};

struct ArtistDetail {
    qint64 id = 0;
    QString name;
    QString picture;
    QString bio;
    int popularity = 0;
    QList<Album> albums;
    QList<Track> topTracks;
    QList<Artist> similarArtists;

    QString pictureUrl(int size = 480) const {
        if (picture.isEmpty()) return {};
        QString u = picture;
        u.replace('-', '/');
        return QStringLiteral("https://resources.tidal.com/images/%1/%2x%2.jpg").arg(u).arg(size);
    }
};

struct StreamManifest {
    enum Type { BTS, MPD };
    Type    type = BTS;
    QString url;           // BTS: direct URL, MPD: manifest content
    QString mimeType;
    QString codec;
    int     sampleRate = 44100;
    int     bitDepth   = 16;
    double  replayGainTrack = 0.0;
    double  replayGainAlbum = 0.0;
};

struct SearchResults {
    QList<Track>    tracks;
    QList<Album>    albums;
    QList<Artist>   artists;
    QList<Playlist> playlists;
    QList<Mix>      mixes;
    int totalTracks    = 0;
    int totalAlbums    = 0;
    int totalArtists   = 0;
    int totalPlaylists = 0;
    int totalMixes     = 0;
};

// One role on a recording and the people credited with it.
struct CreditGroup {
    QString type;               // "Producer", "Bass Guitar": the label's word
    QList<Artist> contributors; // the id is 0 when the name is not an artist page
};

struct TrackCredits {
    QList<CreditGroup> groups;
    QString copyright;
    QString isrc;
    QString releaseDate;   // the album's, in full
    QString upc;

    static CreditGroup groupFromJson(const QJsonObject &j) {
        CreditGroup g;
        g.type = j["type"].toString();
        for (const auto &v : j["contributors"].toArray()) {
            const auto o = v.toObject();
            Artist a;
            a.id   = o["id"].toVariant().toLongLong();
            a.name = o["name"].toString();
            if (!a.name.isEmpty()) g.contributors.append(a);
        }
        return g;
    }
};

} // namespace Tidal
