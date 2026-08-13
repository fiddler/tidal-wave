#pragma once
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QHash>
#include <QSqlDatabase>
#include <QStringList>
#include <QUrl>

class QProcess;
class QSqlQuery;

// Local music library: files the user imported from disk, plus playlists that
// hold them.
//
// These playlists are deliberately local-only. Tidal's API accepts nothing but
// Tidal track ids, so a local file can never be a member of a real Tidal
// playlist. Rather than fake it with a local overlay that silently disagrees
// with every other Tidal client, local playlists are their own thing. Local and
// Tidal tracks still mix freely in the play queue, which is where it matters.
//
// Track maps produced here use the same keys as TidalBridge::trackToMap so the
// existing QML components and the player queue accept them unchanged. The extra
// keys are "localPath" (what the player plays) and "localId" (the row id used
// by the methods below). The "id" key is the negated row id, keeping local
// tracks distinguishable from Tidal ids everywhere else in the app.
class LocalLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(int  trackCount READ trackCount NOTIFY tracksChanged)
    Q_PROPERTY(bool importing  READ importing  NOTIFY importingChanged)
public:
    explicit LocalLibrary(QObject *parent = nullptr);
    ~LocalLibrary() override;

    int  trackCount() const;
    bool importing() const { return m_importing; }

    // Formats we offer to the file dialog and accept on drop. QMediaPlayer
    // handles all of them through the same backend that plays Tidal streams.
    static const QStringList &supportedSuffixes();

    // ─── library ───────────────────────────────────────
    Q_INVOKABLE QVariantList tracks(const QString &filter = QString()) const;
    Q_INVOKABLE void importUrls(const QList<QUrl> &urls);   // files and/or folders
    Q_INVOKABLE void removeTracks(const QVariantList &localIds);
    Q_INVOKABLE bool hasAudioUrls(const QList<QUrl> &urls) const;

    // Re-resolves folder cover art for tracks that still have none. Pure
    // filesystem work (no ffmpeg), so it is cheap enough to run at startup and
    // backfill tracks imported before folder covers were supported. Returns the
    // number of tracks updated.
    Q_INVOKABLE int rescanCovers();

    // ─── playlists ─────────────────────────────────────
    Q_INVOKABLE QVariantList playlists() const;
    Q_INVOKABLE QVariantMap  playlist(qint64 playlistId) const;
    Q_INVOKABLE QVariantList playlistTracks(qint64 playlistId) const;
    Q_INVOKABLE qint64 createPlaylist(const QString &title);
    Q_INVOKABLE void   renamePlaylist(qint64 playlistId, const QString &title);
    Q_INVOKABLE void   deletePlaylist(qint64 playlistId);
    Q_INVOKABLE void   addToPlaylist(qint64 playlistId, const QVariantList &localIds);
    Q_INVOKABLE void   removeFromPlaylist(qint64 playlistId, int position);
    Q_INVOKABLE void   movePlaylistItem(qint64 playlistId, int from, int to);
    // Drag-reorder moves everything that was selected, as one block, to the
    // position the row was dropped at.
    Q_INVOKABLE void   movePlaylistItems(qint64 playlistId, const QVariantList &fromIndices, int toIndex);

signals:
    void tracksChanged();
    void playlistsChanged();
    void importingChanged();
    void importProgress(int done, int total);
    void importFinished(int added, int skipped, int failed);
    void error(const QString &message);

private:
    void openDatabase();
    void createSchema();
    QVariantMap rowToMap(const QSqlQuery &q) const;

    // Import runs one ffprobe at a time, driven by QProcess signals rather than
    // a worker thread, so the UI keeps repainting during a large import and the
    // database is only ever touched from this thread.
    void probeNext();
    void finishImport();
    QString extractCover(const QString &path, qint64 rowId) const;

    // Cover art next to the file, when the file carries none itself. Follows
    // the naming convention every other player uses (cover/folder/front/…);
    // see the implementation for the exact order. Results are cached per
    // directory so a 12-track album scans its folder once, not twelve times.
    QString findFolderCover(const QString &audioPath) const;
    QList<qint64> playlistOrder(qint64 playlistId) const;
    void writePlaylistOrder(qint64 playlistId, const QList<qint64> &order);
    mutable QHash<QString, QString> m_folderCoverCache;

    QSqlDatabase m_db;
    QString      m_coverDir;
    QString      m_ffprobe;
    QString      m_ffmpeg;

    QStringList m_pending;
    int         m_pendingTotal = 0;
    int         m_added = 0, m_skipped = 0, m_failed = 0;
    bool        m_importing = false;
    QProcess   *m_probe = nullptr;
};
