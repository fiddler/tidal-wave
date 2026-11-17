#pragma once
#include <QObject>
#include <QAbstractListModel>
#include "TidalApi.h"
#include "Models.h"

using namespace Tidal;

// ──────────── Generic list model ────────────
template<typename T>
class ListModel : public QAbstractListModel {
public:
    enum { ItemRole = Qt::UserRole + 1 };

    explicit ListModel(QObject *parent = nullptr)
        : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &) const override { return m_items.count(); }

    QVariant data(const QModelIndex &idx, int role) const override {
        if (!idx.isValid() || idx.row() >= m_items.count()) return {};
        if (role == ItemRole) return QVariant::fromValue(m_items[idx.row()]);
        return {};
    }

    QHash<int, QByteArray> roleNames() const override {
        return {{ItemRole, "item"}};
    }

    void setItems(const QList<T> &items) {
        beginResetModel();
        m_items = items;
        endResetModel();
    }

    void append(const T &item) {
        beginInsertRows({}, m_items.size(), m_items.size());
        m_items.append(item);
        endInsertRows();
    }

    void appendList(const QList<T> &items) {
        if (items.isEmpty()) return;
        beginInsertRows({}, m_items.size(), m_items.size() + items.size() - 1);
        m_items.append(items);
        endInsertRows();
    }

    const QList<T> &items() const { return m_items; }
    T item(int i) const { return m_items[i]; }
    int count() const { return m_items.count(); }
    void clear() { setItems({}); }

private:
    QList<T> m_items;
};

// ──────────── High-level API client ────────────
class TidalClient : public QObject {
    Q_OBJECT
public:
    using TracksCallback   = std::function<void(QList<Track>,   QString)>;
    using AlbumsCallback   = std::function<void(QList<Album>,   QString)>;
    using ArtistsCallback  = std::function<void(QList<Artist>,  QString)>;
    using PlaylistsCallback= std::function<void(QList<Playlist>,QString)>;
    using MixesCallback    = std::function<void(QList<Mix>,     QString)>;
    using SearchCb         = std::function<void(SearchResults,  QString)>;
    using StreamCb         = std::function<void(StreamManifest, QString)>;
    using MixPageCallback  = std::function<void(Mix, QList<Track>, QString)>;

    explicit TidalClient(TidalApi *api, QObject *parent = nullptr);

    void setUserId(qint64 uid)        { if (m_userId != uid) { m_userId = uid; emit userIdChanged(uid); } }
    qint64 userId() const             { return m_userId; }
    void setAudioQuality(AudioQuality q) { m_quality = q; }
    AudioQuality audioQuality() const { return m_quality; }

    // Mixes. fetchHomeMixes is the generated feed and the saved list merged on
    // the mix id; neither is a subset of the other.
    void fetchHomeMixes     (MixesCallback cb);
    void fetchGeneratedMixes(MixesCallback cb);
    void fetchSavedMixes    (MixesCallback cb, const QString &cursor = {},
                             QList<Mix> acc = {}, int page = 0);
    // The mix's own header (title, subtitle, art, type) with its tracks, so a
    // mix opened by id alone can still label its page.
    void fetchMixPage     (const QString &mixId, MixPageCallback cb);
    void fetchMixTracks   (const QString &mixId, TracksCallback cb);
    void addMixFavorite   (const QString &mixId, std::function<void(bool)> cb);
    void removeMixFavorite(const QString &mixId, std::function<void(bool)> cb);

    // My collection
    void fetchFavoriteTracks  (TracksCallback    cb, int limit=50, int offset=0);
    void fetchFavoriteAlbums  (AlbumsCallback    cb, int limit=50, int offset=0);
    void fetchFavoriteArtists (ArtistsCallback   cb, int limit=50, int offset=0);
    void fetchUserPlaylists   (PlaylistsCallback cb, int limit=50, int offset=0);

    // Content
    void fetchAlbumTracks  (qint64 albumId,        TracksCallback    cb);
    void fetchPlaylistTracks(const QString &uuid,  TracksCallback    cb);
    void fetchArtistDetail (qint64 artistId,       std::function<void(ArtistDetail,QString)> cb);
    void fetchArtistAlbums (qint64 artistId,       AlbumsCallback    cb);
    void fetchArtistTopTracks(qint64 artistId,     TracksCallback    cb);
    void fetchAlbum        (qint64 albumId,        std::function<void(Album,QString)>  cb);
    // Same, with the HTTP status: 404 is a delisted album, 0 is no network.
    void fetchAlbumStatus  (qint64 albumId,        std::function<void(Album,QString,int)> cb);
    void fetchArtist       (qint64 artistId,       std::function<void(Artist,QString)> cb);
    void fetchTrack        (qint64 trackId,        std::function<void(Track,QString)>  cb);

    // Search
    // `types` narrows the reply to some kinds ("ALBUMS"); empty asks for all
    // five. A page of one tab should not pay for the other four.
    void search(const QString &query, SearchCb cb, int limit = 20, int offset = 0,
                const QString &types = {});

    // Favorites management
    void addTrackFavorite   (qint64 trackId,    std::function<void(bool)> cb);
    void removeTrackFavorite(qint64 trackId,    std::function<void(bool)> cb);
    void addAlbumFavorite    (qint64 albumId,    std::function<void(bool)> cb);
    void removeAlbumFavorite (qint64 albumId,    std::function<void(bool)> cb);
    void addArtistFavorite   (qint64 artistId,   std::function<void(bool)> cb);
    void removeArtistFavorite(qint64 artistId,   std::function<void(bool)> cb);

    // Streaming
    void fetchStreamManifest(qint64 trackId, StreamCb cb);
    // Request a specific quality (e.g. HiResLossless for downloads) without
    // touching the persisted playback preference m_quality.
    void fetchStreamManifest(qint64 trackId, AudioQuality quality, StreamCb cb);
    QNetworkReply* fetchRaw(const QUrl &url, std::function<void(QByteArray, QString)> cb);

    // Playlist management
    void createPlaylist          (const QString &title, std::function<void(Playlist,QString)> cb);
    void fetchPlaylist           (const QString &uuid,  std::function<void(Playlist,QString)> cb);
    void editPlaylist            (const QString &uuid,  const QString &title, const QString &description,
                                  std::function<void(bool)> cb);
    void addTracksToPlaylist     (const QString &uuid,  const QList<qint64> &trackIds, std::function<void(bool)> cb);
    void moveTrackInPlaylist     (const QString &uuid,  int fromIndex, int toIndex, std::function<void(bool)> cb);
    void removeTrackFromPlaylist (const QString &uuid,  int itemIndex,  std::function<void(bool)> cb);

    // Track features
    void fetchTrackRadio(qint64 trackId, TracksCallback cb);
    void fetchLyrics    (qint64 trackId, std::function<void(QString, bool, QString)> cb);
    void fetchTrackCredits(qint64 trackId, std::function<void(TrackCredits, QString)> cb);

    // Recently played
    void fetchRecentlyPlayed(TracksCallback cb);

    // Response parsing. Static and public so tests can feed them fixtures.
    static QList<Track>    parseTracks   (const QJsonObject &root);
    static QList<Album>    parseAlbums   (const QJsonObject &root);
    static QList<Artist>   parseArtists  (const QJsonObject &root);
    static QList<Playlist> parsePlaylists(const QJsonObject &root);
    static SearchResults   parseSearch   (const QJsonObject &root);
    // pages/my_collection_my_mixes, video mixes dropped: the app cannot play one.
    static QList<Mix>      parseMixPage   (const QJsonObject &root);
    static Mix             parseMixHeader (const QJsonObject &root);
    static QList<Track>    parseMixTracks (const QJsonObject &root);
    // v2/favorites/mixes: a flat items array, cursor-paged, no total.
    static QList<Mix>      parseSavedMixes(const QJsonObject &root);
    // The cursor for the next page, or empty when the run is over: an empty
    // page, no cursor, or the same cursor again all end it.
    static QString         nextCursor(const QJsonObject &page, const QString &previous);
    // One row per id, the first list to carry an id fixing its position.
    static QList<Mix>      mergeMixLists  (const QList<QList<Mix>> &lists);
    static QList<Album>    mergeAlbumLists(const QList<QList<Album>> &lists);
    // A 200 is not a yes: the reply has to name the mix among the changed items.
    static bool            mixFavoriteAccepted(const QJsonObject &reply, const QString &mixId,
                                               bool adding);
    static QList<CreditGroup> parseCredits(const QJsonArray &groups);

    static constexpr int kArtistAlbumsPageSize = 50;
    static constexpr int kMaxArtistAlbums      = 500;
    static constexpr int kSavedMixesPageSize   = 50;   // the server's maximum
    static constexpr int kMaxSavedMixesPages   = 20;

signals:
    void error(const QString &msg);
    void userIdChanged(qint64 uid);

private:
    // Pages through `endpoint` 100 items at a time until totalNumberOfItems
    // is reached, then delivers the full accumulated list in one callback —
    // the Tidal API caps each response at 100 items regardless of `limit`.
    void fetchAllTracks(const QString &endpoint, TracksCallback cb,
                        int offset = 0, QList<Track> acc = {});

    // One artists/<id>/albums filter, paged to its end.
    void fetchArtistAlbumPage(qint64 artistId, const QString &filter, AlbumsCallback cb,
                              int offset = 0, QList<Album> acc = {});

    static QString qualityString(AudioQuality q);

    TidalApi     *m_api;
    qint64        m_userId  = 0;
    bool          m_plainAlbumOrder  = false;   // server refused order=DATE
    bool          m_plainArtistOrder = false;
    AudioQuality  m_quality = AudioQuality::Lossless;
};
