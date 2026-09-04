#include "LocalLibrary.h"
#include "../util/MatchScore.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

namespace {

QString msToClock(int seconds) {
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

// ffprobe reports tag names with inconsistent case across containers
// (FLAC uses ARTIST, MP4 uses artist), so look them up case-insensitively.
QString tag(const QJsonObject &tags, const QStringList &names) {
    for (const QString &name : names)
        for (auto it = tags.begin(); it != tags.end(); ++it)
            if (it.key().compare(name, Qt::CaseInsensitive) == 0) {
                const QString v = it.value().toString().trimmed();
                if (!v.isEmpty()) return v;
            }
    return {};
}

// Track tags are often "5/12" rather than "5".
int tagNumber(const QJsonObject &tags, const QStringList &names) {
    const QString raw = tag(tags, names);
    if (raw.isEmpty()) return 0;
    return raw.section('/', 0, 0).toInt();
}

} // namespace

const QStringList &LocalLibrary::supportedSuffixes() {
    static const QStringList s{
        QStringLiteral("flac"), QStringLiteral("mp3"),  QStringLiteral("m4a"),
        QStringLiteral("aac"),  QStringLiteral("ogg"),  QStringLiteral("opus"),
        QStringLiteral("wav"),  QStringLiteral("aiff"), QStringLiteral("aif"),
        QStringLiteral("alac"), QStringLiteral("wma"),  QStringLiteral("mp4"),
    };
    return s;
}

LocalLibrary::LocalLibrary(QObject *parent) : QObject(parent) {
    m_ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    m_ffmpeg  = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    openDatabase();
}

LocalLibrary::~LocalLibrary() {
    if (m_probe) {
        m_probe->disconnect();
        if (m_probe->state() != QProcess::NotRunning) m_probe->kill();
    }
}

void LocalLibrary::openDatabase() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_coverDir = dir + QStringLiteral("/covers");
    QDir().mkpath(m_coverDir);

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("local-library"));
    m_db.setDatabaseName(dir + QStringLiteral("/library.db"));
    if (!m_db.open()) {
        emit error(tr("Could not open the local library: %1").arg(m_db.lastError().text()));
        return;
    }
    QSqlQuery(QStringLiteral("PRAGMA foreign_keys = ON"), m_db);
    createSchema();
    rescanCovers();   // backfill tracks imported before folder covers existed
}

void LocalLibrary::createSchema() {
    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS tracks ("
        " id INTEGER PRIMARY KEY,"
        " path TEXT NOT NULL UNIQUE,"
        " title TEXT NOT NULL,"
        " artist TEXT, album TEXT, year TEXT, codec TEXT, cover TEXT,"
        " duration INTEGER NOT NULL DEFAULT 0,"
        " track_no INTEGER NOT NULL DEFAULT 0,"
        " disc_no  INTEGER NOT NULL DEFAULT 1,"
        " added_at INTEGER NOT NULL DEFAULT 0)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS playlists ("
        " id INTEGER PRIMARY KEY,"
        " title TEXT NOT NULL,"
        " created_at INTEGER NOT NULL DEFAULT 0,"
        " updated_at INTEGER NOT NULL DEFAULT 0)"));
    // Deleting a track drops it from every playlist that holds it; positions
    // are renumbered by the code that removes rows.
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS playlist_items ("
        " playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,"
        " track_id    INTEGER NOT NULL REFERENCES tracks(id)    ON DELETE CASCADE,"
        " position    INTEGER NOT NULL)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_items_playlist ON playlist_items(playlist_id, position)"));
}

int LocalLibrary::trackCount() const {
    QSqlQuery q(QStringLiteral("SELECT COUNT(*) FROM tracks"), m_db);
    return q.next() ? q.value(0).toInt() : 0;
}

QVariantMap LocalLibrary::rowToMap(const QSqlQuery &q) const {
    const qint64  id       = q.value(QStringLiteral("id")).toLongLong();
    const int     duration = q.value(QStringLiteral("duration")).toInt();
    const QString cover    = q.value(QStringLiteral("cover")).toString();
    const QString coverUrl = cover.isEmpty() ? QString() : QUrl::fromLocalFile(cover).toString();

    QVariantMap m;
    // Same shape as TidalBridge::trackToMap so existing components just work.
    m[QStringLiteral("id")]          = -id;   // negative: never a valid Tidal id
    m[QStringLiteral("localId")]     = id;
    m[QStringLiteral("localPath")]   = q.value(QStringLiteral("path")).toString();
    m[QStringLiteral("title")]       = q.value(QStringLiteral("title")).toString();
    m[QStringLiteral("artists")]     = q.value(QStringLiteral("artist")).toString();
    m[QStringLiteral("artistId")]    = 0LL;
    m[QStringLiteral("albumTitle")]  = q.value(QStringLiteral("album")).toString();
    m[QStringLiteral("albumId")]     = 0LL;
    m[QStringLiteral("albumCover")]  = QString();
    m[QStringLiteral("coverUrl")]    = coverUrl;
    m[QStringLiteral("coverUrl80")]  = coverUrl;
    m[QStringLiteral("duration")]    = duration;
    m[QStringLiteral("durationStr")] = msToClock(duration);
    m[QStringLiteral("trackNumber")] = q.value(QStringLiteral("track_no")).toInt();
    m[QStringLiteral("explicit_")]   = false;
    m[QStringLiteral("quality")]     = q.value(QStringLiteral("codec")).toString().toUpper();
    m[QStringLiteral("popularity")]  = 0;
    m[QStringLiteral("available")]   = true;   // a local file is always playable
    m[QStringLiteral("isLocal")]     = true;
    return m;
}

QVariantList LocalLibrary::tracks(const QString &filter) const {
    QSqlQuery q(m_db);
    if (filter.trimmed().isEmpty()) {
        q.prepare(QStringLiteral("SELECT * FROM tracks ORDER BY artist, album, disc_no, track_no, title"));
    } else {
        q.prepare(QStringLiteral(
            "SELECT * FROM tracks WHERE title LIKE :f OR artist LIKE :f OR album LIKE :f"
            " ORDER BY artist, album, disc_no, track_no, title"));
        q.bindValue(QStringLiteral(":f"), QStringLiteral("%%%1%%").arg(filter.trimmed()));
    }
    QVariantList out;
    if (q.exec()) while (q.next()) out.append(rowToMap(q));
    return out;
}

QVariantMap LocalLibrary::searchTracks(const QString &query, int limit) const {
    QVariantMap out;
    out[QStringLiteral("rows")]  = QVariantList();
    out[QStringLiteral("total")] = 0;

    const QString needle = query.trimmed();
    if (needle.isEmpty() || limit <= 0) return out;
    const QString lowered = needle.toLower();
    const QString like    = QStringLiteral("%%%1%%").arg(needle);

    QSqlQuery count(m_db);
    count.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM tracks WHERE title LIKE :f OR artist LIKE :f OR album LIKE :f"));
    count.bindValue(QStringLiteral(":f"), like);
    if (count.exec() && count.next()) out[QStringLiteral("total")] = count.value(0).toInt();

    // A one-letter query matches most of the library, so the exact ranking is
    // done in C++ over a bounded candidate set rather than over every row. The
    // SQL ordering puts title-prefix hits in that set first, so the candidates
    // are the rows most likely to win anyway.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT * FROM tracks WHERE title LIKE :f OR artist LIKE :f OR album LIKE :f"
        " ORDER BY (CASE WHEN title LIKE :pre THEN 0 WHEN artist LIKE :pre THEN 1"
        "                WHEN album LIKE :pre THEN 2 ELSE 3 END),"
        " artist, album, disc_no, track_no, title LIMIT 200"));
    q.bindValue(QStringLiteral(":f"),   like);
    q.bindValue(QStringLiteral(":pre"), QStringLiteral("%1%%").arg(needle));
    if (!q.exec()) return out;

    QList<QPair<int, QVariantMap>> scored;
    while (q.next()) {
        QVariantMap m = rowToMap(q);
        const int s = MatchScore::score(m.value(QStringLiteral("title")).toString(),
                                        {m.value(QStringLiteral("artists")).toString(),
                                         m.value(QStringLiteral("albumTitle")).toString()},
                                        lowered);
        if (s < 0) continue;
        m[QStringLiteral("_score")] = s;
        scored.append({s, m});
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });

    QVariantList rows;
    for (int i = 0; i < scored.size() && i < limit; ++i) rows.append(scored[i].second);
    out[QStringLiteral("rows")] = rows;
    return out;
}

QVariantMap LocalLibrary::searchPlaylists(const QString &query, int limit) const {
    QVariantMap out;
    out[QStringLiteral("rows")]  = QVariantList();
    out[QStringLiteral("total")] = 0;

    const QString lowered = query.trimmed().toLower();
    if (lowered.isEmpty() || limit <= 0) return out;

    // Local playlists are a handful, so the whole list is scored in place.
    QList<QPair<int, QVariantMap>> scored;
    for (const QVariant &v : playlists()) {
        QVariantMap m = v.toMap();
        const int s = MatchScore::score(m.value(QStringLiteral("title")).toString(), {}, lowered);
        if (s < 0) continue;
        m[QStringLiteral("_score")] = s;
        scored.append({s, m});
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });

    QVariantList rows;
    for (int i = 0; i < scored.size() && i < limit; ++i) rows.append(scored[i].second);
    out[QStringLiteral("rows")]  = rows;
    out[QStringLiteral("total")] = scored.size();
    return out;
}

bool LocalLibrary::hasAudioUrls(const QList<QUrl> &urls) const {
    for (const QUrl &u : urls) {
        if (!u.isLocalFile()) continue;
        const QFileInfo fi(u.toLocalFile());
        if (fi.isDir()) return true;
        if (supportedSuffixes().contains(fi.suffix().toLower())) return true;
    }
    return false;
}

// ─── import ────────────────────────────────────────────

void LocalLibrary::importUrls(const QList<QUrl> &urls) {
    if (m_importing) {
        emit error(tr("An import is already running."));
        return;
    }
    if (m_ffprobe.isEmpty()) {
        emit error(tr("ffprobe was not found on PATH — install ffmpeg to import local files."));
        return;
    }

    QStringList files;
    for (const QUrl &u : urls) {
        if (!u.isLocalFile()) continue;
        const QString path = u.toLocalFile();
        const QFileInfo fi(path);
        if (fi.isDir()) {
            QDirIterator it(path, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QFileInfo f(it.next());
                if (supportedSuffixes().contains(f.suffix().toLower()))
                    files << f.absoluteFilePath();
            }
        } else if (supportedSuffixes().contains(fi.suffix().toLower())) {
            files << fi.absoluteFilePath();
        }
    }
    files.removeDuplicates();

    if (files.isEmpty()) {
        emit importFinished(0, 0, 0);
        return;
    }

    m_folderCoverCache.clear();
    m_pending      = files;
    m_pendingTotal = files.size();
    m_added = m_skipped = m_failed = 0;
    m_importing = true;
    emit importingChanged();
    emit importProgress(0, m_pendingTotal);
    probeNext();
}

void LocalLibrary::probeNext() {
    if (m_pending.isEmpty()) { finishImport(); return; }

    const QString path = m_pending.takeFirst();

    // Already imported — nothing to do. Cheaper than probing then failing on
    // the UNIQUE constraint.
    QSqlQuery dup(m_db);
    dup.prepare(QStringLiteral("SELECT 1 FROM tracks WHERE path = :p"));
    dup.bindValue(QStringLiteral(":p"), path);
    if (dup.exec() && dup.next()) {
        ++m_skipped;
        emit importProgress(m_pendingTotal - m_pending.size(), m_pendingTotal);
        QMetaObject::invokeMethod(this, &LocalLibrary::probeNext, Qt::QueuedConnection);
        return;
    }

    m_probe = new QProcess(this);
    m_probe->setProgram(m_ffprobe);
    m_probe->setArguments({QStringLiteral("-v"), QStringLiteral("quiet"),
                           QStringLiteral("-print_format"), QStringLiteral("json"),
                           QStringLiteral("-show_format"), QStringLiteral("-show_streams"),
                           path});

    connect(m_probe, &QProcess::finished, this, [this, path](int code, QProcess::ExitStatus) {
        const QByteArray out = m_probe->readAllStandardOutput();
        m_probe->deleteLater();
        m_probe = nullptr;

        if (code == 0) {
            const QJsonObject root   = QJsonDocument::fromJson(out).object();
            const QJsonObject format = root.value(QStringLiteral("format")).toObject();
            const QJsonObject tags   = format.value(QStringLiteral("tags")).toObject();

            QString codec;
            bool hasCoverStream = false;
            for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
                const QJsonObject s = v.toObject();
                const QString type = s.value(QStringLiteral("codec_type")).toString();
                if (type == QLatin1String("audio") && codec.isEmpty())
                    codec = s.value(QStringLiteral("codec_name")).toString();
                if (type == QLatin1String("video")
                    && s.value(QStringLiteral("disposition")).toObject()
                         .value(QStringLiteral("attached_pic")).toInt() == 1)
                    hasCoverStream = true;
            }

            const QFileInfo fi(path);
            QString title = tag(tags, {QStringLiteral("title")});
            if (title.isEmpty()) title = fi.completeBaseName();   // untagged file

            QSqlQuery ins(m_db);
            ins.prepare(QStringLiteral(
                "INSERT INTO tracks (path,title,artist,album,year,codec,duration,track_no,disc_no,added_at)"
                " VALUES (:path,:title,:artist,:album,:year,:codec,:duration,:track_no,:disc_no,:added)"));
            ins.bindValue(QStringLiteral(":path"),  path);
            ins.bindValue(QStringLiteral(":title"), title);
            ins.bindValue(QStringLiteral(":artist"),
                          tag(tags, {QStringLiteral("artist"), QStringLiteral("album_artist")}));
            ins.bindValue(QStringLiteral(":album"), tag(tags, {QStringLiteral("album")}));
            ins.bindValue(QStringLiteral(":year"),
                          tag(tags, {QStringLiteral("date"), QStringLiteral("year")}));
            ins.bindValue(QStringLiteral(":codec"), codec);
            ins.bindValue(QStringLiteral(":duration"),
                          qRound(format.value(QStringLiteral("duration")).toString().toDouble()));
            ins.bindValue(QStringLiteral(":track_no"),
                          tagNumber(tags, {QStringLiteral("track")}));
            ins.bindValue(QStringLiteral(":disc_no"),
                          qMax(1, tagNumber(tags, {QStringLiteral("disc")})));
            ins.bindValue(QStringLiteral(":added"), QDateTime::currentMSecsSinceEpoch());

            if (ins.exec()) {
                ++m_added;
                const qint64 rowId = ins.lastInsertId().toLongLong();
                // Embedded art wins: it is per-track, so it stays correct on
                // compilations where one folder image would not be.
                QString cover = hasCoverStream ? extractCover(path, rowId) : QString();
                if (cover.isEmpty()) cover = findFolderCover(path);
                if (!cover.isEmpty()) {
                    QSqlQuery up(m_db);
                    up.prepare(QStringLiteral("UPDATE tracks SET cover = :c WHERE id = :id"));
                    up.bindValue(QStringLiteral(":c"), cover);
                    up.bindValue(QStringLiteral(":id"), rowId);
                    up.exec();
                }
            } else {
                ++m_failed;
            }
        } else {
            ++m_failed;
        }

        emit importProgress(m_pendingTotal - m_pending.size(), m_pendingTotal);
        probeNext();
    });

    connect(m_probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_probe) return;
        m_probe->deleteLater();
        m_probe = nullptr;
        ++m_failed;
        probeNext();
    });

    m_probe->start();
}

QString LocalLibrary::extractCover(const QString &path, qint64 rowId) const {
    if (m_ffmpeg.isEmpty()) return {};
    const QString out = QStringLiteral("%1/%2.jpg").arg(m_coverDir).arg(rowId);
    QProcess p;
    p.start(m_ffmpeg, {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-y"),
                       QStringLiteral("-i"), path,
                       QStringLiteral("-map"), QStringLiteral("0:v:0"),
                       QStringLiteral("-frames:v"), QStringLiteral("1"),
                       out});
    if (!p.waitForFinished(5000) || p.exitCode() != 0) {
        QFile::remove(out);
        return {};
    }
    return QFileInfo::exists(out) ? out : QString();
}

QString LocalLibrary::findFolderCover(const QString &audioPath) const {
    const QFileInfo audio(audioPath);
    const QString   dirPath = audio.absolutePath();

    const auto cached = m_folderCoverCache.constFind(dirPath);
    if (cached != m_folderCoverCache.constEnd()) return *cached;

    static const QStringList imageSuffixes{
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("gif")};
    // Priority order. cover.* is the common Unix default (MPD, Kodi, beets,
    // Picard); folder.* comes from Windows Media Player; front.* from CD
    // ripping tools. Matched case-insensitively — a case-sensitive match would
    // miss "Cover.jpg" on Linux.
    static const QStringList conventional{
        QStringLiteral("cover"), QStringLiteral("folder"), QStringLiteral("front"),
        QStringLiteral("album"), QStringLiteral("albumart"), QStringLiteral("art"),
        QStringLiteral("thumb")};

    QFileInfoList images;
    for (const QFileInfo &f : QDir(dirPath).entryInfoList(
             QDir::Files | QDir::NoSymLinks | QDir::Hidden, QDir::Name)) {
        if (imageSuffixes.contains(f.suffix().toLower())) images << f;
    }

    // Amarok writes ".folder.png"; drop a leading dot before comparing.
    auto stem = [](const QFileInfo &f) {
        QString b = f.completeBaseName();
        if (b.startsWith(QLatin1Char('.'))) b = b.mid(1);
        return b;
    };

    QString found;
    for (const QString &name : conventional) {
        for (const QFileInfo &f : images) {
            if (stem(f).compare(name, Qt::CaseInsensitive) == 0) { found = f.absoluteFilePath(); break; }
        }
        if (!found.isEmpty()) break;
    }

    // Windows Media Player's own cache files, e.g. AlbumArt_{GUID}_Large.jpg.
    if (found.isEmpty()) {
        for (const QFileInfo &f : images) {
            const QString b = stem(f);
            if (b.startsWith(QLatin1String("AlbumArt"), Qt::CaseInsensitive)
                && b.endsWith(QLatin1String("Large"), Qt::CaseInsensitive)) {
                found = f.absoluteFilePath();
                break;
            }
        }
    }

    // An image named after the track itself — the single-file album case.
    if (found.isEmpty()) {
        for (const QFileInfo &f : images) {
            if (stem(f).compare(audio.completeBaseName(), Qt::CaseInsensitive) == 0) {
                found = f.absoluteFilePath();
                break;
            }
        }
    }

    // Last resort: the only image in the folder. Deliberately requires exactly
    // one — a folder full of booklet scans (scan_01.jpg, scan_02.jpg …) has no
    // way to say which one is the front, so it gets no cover instead of a
    // random page.
    if (found.isEmpty() && images.size() == 1) found = images.first().absoluteFilePath();

    m_folderCoverCache.insert(dirPath, found);
    return found;
}

int LocalLibrary::rescanCovers() {
    m_folderCoverCache.clear();

    QSqlQuery q(QStringLiteral(
        "SELECT id, path FROM tracks WHERE cover IS NULL OR cover = ''"), m_db);
    QList<QPair<qint64, QString>> rows;
    while (q.next()) rows.append({q.value(0).toLongLong(), q.value(1).toString()});

    int updated = 0;
    m_db.transaction();
    for (const auto &row : rows) {
        const QString cover = findFolderCover(row.second);
        if (cover.isEmpty()) continue;
        QSqlQuery up(m_db);
        up.prepare(QStringLiteral("UPDATE tracks SET cover = :c WHERE id = :id"));
        up.bindValue(QStringLiteral(":c"), cover);
        up.bindValue(QStringLiteral(":id"), row.first);
        if (up.exec()) ++updated;
    }
    m_db.commit();

    if (updated > 0) emit tracksChanged();
    return updated;
}

void LocalLibrary::finishImport() {
    m_importing = false;
    m_pendingTotal = 0;
    emit importingChanged();
    emit tracksChanged();
    emit importFinished(m_added, m_skipped, m_failed);
}

void LocalLibrary::removeTracks(const QVariantList &localIds) {
    if (localIds.isEmpty()) return;
    m_db.transaction();
    for (const QVariant &v : localIds) {
        const qint64 id = v.toLongLong();
        QSqlQuery cover(m_db);
        cover.prepare(QStringLiteral("SELECT cover FROM tracks WHERE id = :id"));
        cover.bindValue(QStringLiteral(":id"), id);
        if (cover.exec() && cover.next()) {
            const QString c = cover.value(0).toString();
            if (!c.isEmpty()) QFile::remove(c);
        }
        QSqlQuery del(m_db);
        del.prepare(QStringLiteral("DELETE FROM tracks WHERE id = :id"));
        del.bindValue(QStringLiteral(":id"), id);
        del.exec();
    }
    m_db.commit();
    // The cascade leaves gaps in the position sequence of any playlist that
    // held a deleted track, so renumber every playlist.
    QSqlQuery pl(QStringLiteral("SELECT id FROM playlists"), m_db);
    QList<qint64> ids;
    while (pl.next()) ids << pl.value(0).toLongLong();
    for (qint64 pid : ids) {
        QSqlQuery items(m_db);
        items.prepare(QStringLiteral("SELECT rowid FROM playlist_items WHERE playlist_id = :p ORDER BY position"));
        items.bindValue(QStringLiteral(":p"), pid);
        int pos = 0;
        QList<qint64> rows;
        if (items.exec()) while (items.next()) rows << items.value(0).toLongLong();
        for (qint64 r : rows) {
            QSqlQuery up(m_db);
            up.prepare(QStringLiteral("UPDATE playlist_items SET position = :pos WHERE rowid = :r"));
            up.bindValue(QStringLiteral(":pos"), pos++);
            up.bindValue(QStringLiteral(":r"), r);
            up.exec();
        }
    }
    emit tracksChanged();
    emit playlistsChanged();
}

// ─── playlists ─────────────────────────────────────────

QVariantList LocalLibrary::playlists() const {
    QSqlQuery q(QStringLiteral(
        "SELECT p.id, p.title, p.updated_at, COUNT(i.rowid) AS n,"
        " COALESCE(SUM(t.duration), 0) AS total"
        " FROM playlists p"
        " LEFT JOIN playlist_items i ON i.playlist_id = p.id"
        " LEFT JOIN tracks t ON t.id = i.track_id"
        " GROUP BY p.id ORDER BY p.title COLLATE NOCASE"), m_db);
    QVariantList out;
    while (q.next()) {
        QVariantMap m;
        m[QStringLiteral("id")]        = q.value(0).toLongLong();
        m[QStringLiteral("title")]     = q.value(1).toString();
        m[QStringLiteral("updatedAt")] = q.value(2).toLongLong();
        m[QStringLiteral("numTracks")] = q.value(3).toInt();
        m[QStringLiteral("duration")]  = q.value(4).toInt();
        out.append(m);
    }
    return out;
}

QVariantMap LocalLibrary::playlist(qint64 playlistId) const {
    for (const QVariant &v : playlists()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toLongLong() == playlistId) return m;
    }
    return {};
}

QVariantList LocalLibrary::playlistTracks(qint64 playlistId) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT t.* FROM playlist_items i JOIN tracks t ON t.id = i.track_id"
        " WHERE i.playlist_id = :p ORDER BY i.position"));
    q.bindValue(QStringLiteral(":p"), playlistId);
    QVariantList out;
    if (q.exec()) while (q.next()) out.append(rowToMap(q));
    return out;
}

qint64 LocalLibrary::createPlaylist(const QString &title) {
    const QString name = title.trimmed();
    if (name.isEmpty()) return -1;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO playlists (title, created_at, updated_at)"
                             " VALUES (:t, :c, :u)"));
    q.bindValue(QStringLiteral(":t"), name);
    q.bindValue(QStringLiteral(":c"), now);
    q.bindValue(QStringLiteral(":u"), now);
    if (!q.exec()) {
        emit error(tr("Could not create the playlist: %1").arg(q.lastError().text()));
        return -1;
    }
    emit playlistsChanged();
    return q.lastInsertId().toLongLong();
}

void LocalLibrary::renamePlaylist(qint64 playlistId, const QString &title) {
    const QString name = title.trimmed();
    if (name.isEmpty()) return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE playlists SET title = :t, updated_at = :u WHERE id = :id"));
    q.bindValue(QStringLiteral(":t"), name);
    q.bindValue(QStringLiteral(":u"), QDateTime::currentMSecsSinceEpoch());
    q.bindValue(QStringLiteral(":id"), playlistId);
    if (q.exec()) emit playlistsChanged();
}

void LocalLibrary::deletePlaylist(qint64 playlistId) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM playlists WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), playlistId);
    if (q.exec()) emit playlistsChanged();
}

void LocalLibrary::addToPlaylist(qint64 playlistId, const QVariantList &localIds) {
    if (localIds.isEmpty()) return;

    QSqlQuery next(m_db);
    next.prepare(QStringLiteral("SELECT COALESCE(MAX(position) + 1, 0) FROM playlist_items"
                                " WHERE playlist_id = :p"));
    next.bindValue(QStringLiteral(":p"), playlistId);
    int pos = (next.exec() && next.next()) ? next.value(0).toInt() : 0;

    m_db.transaction();
    for (const QVariant &v : localIds) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("INSERT INTO playlist_items (playlist_id, track_id, position)"
                                 " VALUES (:p, :t, :pos)"));
        q.bindValue(QStringLiteral(":p"), playlistId);
        q.bindValue(QStringLiteral(":t"), v.toLongLong());
        q.bindValue(QStringLiteral(":pos"), pos++);
        q.exec();
    }
    m_db.commit();

    QSqlQuery touch(m_db);
    touch.prepare(QStringLiteral("UPDATE playlists SET updated_at = :u WHERE id = :id"));
    touch.bindValue(QStringLiteral(":u"), QDateTime::currentMSecsSinceEpoch());
    touch.bindValue(QStringLiteral(":id"), playlistId);
    touch.exec();

    emit playlistsChanged();
}

void LocalLibrary::removeFromPlaylist(qint64 playlistId, int position) {
    QSqlQuery del(m_db);
    del.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id = :p AND position = :pos"));
    del.bindValue(QStringLiteral(":p"), playlistId);
    del.bindValue(QStringLiteral(":pos"), position);
    if (!del.exec()) return;

    QSqlQuery shift(m_db);
    shift.prepare(QStringLiteral("UPDATE playlist_items SET position = position - 1"
                                 " WHERE playlist_id = :p AND position > :pos"));
    shift.bindValue(QStringLiteral(":p"), playlistId);
    shift.bindValue(QStringLiteral(":pos"), position);
    shift.exec();

    emit playlistsChanged();
}

QList<qint64> LocalLibrary::playlistOrder(qint64 playlistId) const {
    QSqlQuery ids(m_db);
    ids.prepare(QStringLiteral("SELECT track_id FROM playlist_items WHERE playlist_id = :p"
                               " ORDER BY position"));
    ids.bindValue(QStringLiteral(":p"), playlistId);
    QList<qint64> order;
    if (ids.exec()) while (ids.next()) order << ids.value(0).toLongLong();
    return order;
}

// Rewrite the whole sequence: simpler than shuffling positions around, and
// these lists are small enough that it costs nothing.
void LocalLibrary::writePlaylistOrder(qint64 playlistId, const QList<qint64> &order) {
    m_db.transaction();
    QSqlQuery clear(m_db);
    clear.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id = :p"));
    clear.bindValue(QStringLiteral(":p"), playlistId);
    clear.exec();
    for (int i = 0; i < order.size(); ++i) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("INSERT INTO playlist_items (playlist_id, track_id, position)"
                                 " VALUES (:p, :t, :pos)"));
        q.bindValue(QStringLiteral(":p"), playlistId);
        q.bindValue(QStringLiteral(":t"), order.at(i));
        q.bindValue(QStringLiteral(":pos"), i);
        q.exec();
    }
    m_db.commit();
    emit playlistsChanged();
}

void LocalLibrary::movePlaylistItem(qint64 playlistId, int from, int to) {
    if (from == to) return;
    QList<qint64> order = playlistOrder(playlistId);
    if (from < 0 || from >= order.size() || to < 0 || to >= order.size()) return;
    order.move(from, to);
    writePlaylistOrder(playlistId, order);
}

void LocalLibrary::movePlaylistItems(qint64 playlistId, const QVariantList &fromIndices, int toIndex) {
    QList<qint64> order = playlistOrder(playlistId);

    QList<int> from;
    for (const QVariant &v : fromIndices) {
        const int i = v.toInt();
        if (i >= 0 && i < order.size() && !from.contains(i)) from << i;
    }
    if (from.isEmpty()) return;
    std::sort(from.begin(), from.end());

    // toIndex counts positions in the list as it looks now, so subtract the
    // moved rows that sit above the drop point — they are about to leave.
    int above = 0;
    for (int i : from) if (i < toIndex) ++above;
    const int target = qBound(0, toIndex - above, order.size() - from.size());

    QList<qint64> moved;
    for (int i : from) moved << order.at(i);
    for (int k = from.size() - 1; k >= 0; --k) order.removeAt(from.at(k));
    for (int k = 0; k < moved.size(); ++k) order.insert(target + k, moved.at(k));

    writePlaylistOrder(playlistId, order);
}
