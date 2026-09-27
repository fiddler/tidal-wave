#include "LocalLibrary.h"
#include "CueSheet.h"
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
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

namespace {

// A .app launched from Finder or the Dock inherits a bare PATH —
// /usr/bin:/bin:/usr/sbin:/sbin — so findExecutable misses every package
// manager's prefix, and an import that works from a terminal fails from the
// Dock with "not found". Look in the usual prefixes as well.
QString findTool(const QString &name) {
    const QString onPath = QStandardPaths::findExecutable(name);
    if (!onPath.isEmpty()) return onPath;
    static const QStringList prefixes{
        QStringLiteral("/opt/homebrew/bin"),   // Homebrew, Apple silicon
        QStringLiteral("/usr/local/bin"),      // Homebrew, Intel
        QStringLiteral("/opt/local/bin"),      // MacPorts
        QStringLiteral("/usr/bin"),
        QStringLiteral("/snap/bin"),
    };
    return QStandardPaths::findExecutable(name, prefixes);
}

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

// A user's query is a literal, so its LIKE metacharacters have to stop being
// wildcards: without this, "%" matches the whole library and the match count
// stops agreeing with what was actually ranked. Pairs with the ESCAPE clause.
QString escapeLike(const QString &s) {
    QString out;
    out.reserve(s.size() + 4);
    for (const QChar &c : s) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('%') || c == QLatin1Char('_'))
            out += QLatin1Char('\\');
        out += c;
    }
    return out;
}

// Track tags are often "5/12" rather than "5".
int tagNumber(const QJsonObject &tags, const QStringList &names) {
    const QString raw = tag(tags, names);
    if (raw.isEmpty()) return 0;
    return raw.section('/', 0, 0).toInt();
}

bool isCue(const QFileInfo &f) {
    return f.suffix().compare(QLatin1String("cue"), Qt::CaseInsensitive) == 0;
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
    resolveTools();
    openDatabase();
}

LocalLibrary::~LocalLibrary() {
    if (m_probe) {
        m_probe->disconnect();
        if (m_probe->state() != QProcess::NotRunning) m_probe->kill();
    }
}

// Re-run before every import, not only at startup: installing ffmpeg is the
// fix we tell the user to apply, and it should take effect without a restart.
void LocalLibrary::resolveTools() {
    if (m_ffprobe.isEmpty()) m_ffprobe = findTool(QStringLiteral("ffprobe"));
    if (m_ffmpeg.isEmpty())  m_ffmpeg  = findTool(QStringLiteral("ffmpeg"));
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
        " path TEXT NOT NULL,"
        " title TEXT NOT NULL,"
        " artist TEXT, album TEXT, album_artist TEXT, year TEXT, codec TEXT, cover TEXT,"
        " duration INTEGER NOT NULL DEFAULT 0,"
        " track_no INTEGER NOT NULL DEFAULT 0,"
        " disc_no  INTEGER NOT NULL DEFAULT 1,"
        " added_at INTEGER NOT NULL DEFAULT 0,"
        " start_ms INTEGER NOT NULL DEFAULT 0,"
        " end_ms   INTEGER NOT NULL DEFAULT 0,"
        " UNIQUE (path, start_ms))"));
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

    migrateAlbumArtist();
    migrateSlices();
}

// album_artist arrived after the first libraries were built, and it is what
// keeps a split or compilation album in one block instead of scattering its
// tracks under each performer's name.
//
// Existing rows are filled in from what is already in the table rather than by
// re-running ffprobe over the whole library: an album whose tracks all name the
// same artist belongs to that artist, and one that names several is a
// compilation. That is the case the scattering actually comes from, it costs
// two statements instead of a process per file, and a later re-import of any
// file overwrites the guess with the real tag.
void LocalLibrary::migrateAlbumArtist() {
    QSqlQuery cols(QStringLiteral("PRAGMA table_info(tracks)"), m_db);
    bool has = false;
    while (cols.next())
        if (cols.value(1).toString() == QLatin1String("album_artist")) { has = true; break; }
    if (!has) {
        QSqlQuery add(m_db);
        if (!add.exec(QStringLiteral("ALTER TABLE tracks ADD COLUMN album_artist TEXT"))) return;
    }

    QSqlQuery fill(m_db);
    fill.exec(QStringLiteral(
        "UPDATE tracks SET album_artist = ("
        "  SELECT CASE WHEN COUNT(DISTINCT o.artist) > 1 THEN 'Various Artists'"
        "              ELSE MIN(o.artist) END"
        "  FROM tracks o WHERE o.album = tracks.album)"
        " WHERE (album_artist IS NULL OR album_artist = '')"
        "   AND album IS NOT NULL AND album <> ''"));
    fill.exec(QStringLiteral(
        "UPDATE tracks SET album_artist = artist"
        " WHERE album_artist IS NULL OR album_artist = ''"));
}

// start_ms/end_ms let one file hold a whole CUE-sheet album, which also means
// path stops being unique on its own. SQLite cannot change a constraint in
// place, so the table is rebuilt: foreign keys go off first, or dropping the
// old table would cascade into every playlist.
void LocalLibrary::migrateSlices() {
    // The first build with slices created fresh tables that still said
    // "path TEXT NOT NULL UNIQUE", so the column alone is not proof.
    QSqlQuery ddl(QStringLiteral("SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'tracks'"), m_db);
    if (ddl.next()) {
        const QString sql = ddl.value(0).toString();
        // An open statement on sqlite_master makes the DROP below fail with
        // "table is locked".
        ddl.finish();
        if (sql.contains(QLatin1String("start_ms"))
            && !sql.contains(QLatin1String("path TEXT NOT NULL UNIQUE"), Qt::CaseInsensitive))
            return;
    }

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = OFF"));
    m_db.transaction();
    QString columns = QStringLiteral(
        "id,path,title,artist,album,album_artist,year,codec,cover,duration,track_no,disc_no,added_at");
    QSqlQuery cols(QStringLiteral("PRAGMA table_info(tracks)"), m_db);
    while (cols.next())
        if (cols.value(1).toString() == QLatin1String("start_ms")) {
            columns += QStringLiteral(",start_ms,end_ms");
            break;
        }
    cols.finish();
    const bool ok =
        q.exec(QStringLiteral(
            "CREATE TABLE tracks_new ("
            " id INTEGER PRIMARY KEY,"
            " path TEXT NOT NULL,"
            " title TEXT NOT NULL,"
            " artist TEXT, album TEXT, album_artist TEXT, year TEXT, codec TEXT, cover TEXT,"
            " duration INTEGER NOT NULL DEFAULT 0,"
            " track_no INTEGER NOT NULL DEFAULT 0,"
            " disc_no  INTEGER NOT NULL DEFAULT 1,"
            " added_at INTEGER NOT NULL DEFAULT 0,"
            " start_ms INTEGER NOT NULL DEFAULT 0,"
            " end_ms   INTEGER NOT NULL DEFAULT 0,"
            " UNIQUE (path, start_ms))"))
        && q.exec(QStringLiteral("INSERT INTO tracks_new (%1) SELECT %1 FROM tracks").arg(columns))
        && q.exec(QStringLiteral("DROP TABLE tracks"))
        && q.exec(QStringLiteral("ALTER TABLE tracks_new RENAME TO tracks"));
    if (ok) m_db.commit();
    else {
        emit error(tr("Could not upgrade the local library: %1").arg(q.lastError().text()));
        m_db.rollback();
    }
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
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
    m[QStringLiteral("albumArtist")] = q.value(QStringLiteral("album_artist")).toString();
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
    // Non-zero for a track cut from a CUE-sheet album: the player plays only
    // this range of localPath. An end of zero means the end of the file.
    m[QStringLiteral("localStartMs")] = q.value(QStringLiteral("start_ms")).toLongLong();
    m[QStringLiteral("localEndMs")]   = q.value(QStringLiteral("end_ms")).toLongLong();
    return m;
}

// Every clause ends in album, disc_no, track_no: whatever the list is sorted
// by, an album still reads in its own running order rather than alphabetically
// by song title. album_artist leads the default so a split stays in one block.
QString LocalLibrary::orderClause(const QString &sort) {
    if (sort == QLatin1String("album"))    return QStringLiteral("album COLLATE NOCASE, disc_no, track_no, title COLLATE NOCASE");
    if (sort == QLatin1String("title"))    return QStringLiteral("title COLLATE NOCASE, artist COLLATE NOCASE");
    if (sort == QLatin1String("duration")) return QStringLiteral("duration, title COLLATE NOCASE");
    if (sort == QLatin1String("added"))    return QStringLiteral("added_at DESC, id DESC");
    return QStringLiteral("album_artist COLLATE NOCASE, album COLLATE NOCASE, disc_no, track_no, title COLLATE NOCASE");
}

QVariantList LocalLibrary::tracks(const QString &filter, const QString &sort) const {
    const QString order = QStringLiteral(" ORDER BY ") + orderClause(sort);
    QSqlQuery q(m_db);
    if (filter.trimmed().isEmpty()) {
        q.prepare(QStringLiteral("SELECT * FROM tracks") + order);
    } else {
        q.prepare(QStringLiteral(
            "SELECT * FROM tracks WHERE title LIKE :f OR artist LIKE :f OR album LIKE :f"
            " OR album_artist LIKE :f") + order);
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

    // Two passes. The first scores straight off the columns and allocates
    // nothing per row, so the ranking sees every match rather than whichever
    // subset a LIMIT happened to keep — and "total" is then the real number of
    // matches, not a LIKE count that disagrees with what was ranked.
    //
    // LIKE is a prefilter, never the ranking, and only when the query is pure
    // ASCII: SQLite folds case for ASCII alone, so an "ä" prefilter would drop
    // exactly the rows a Finnish or German library needs it to find. A
    // non-ASCII query scans instead — rare, and correct.
    bool ascii = true;
    for (const QChar &c : needle) if (c.unicode() > 127) { ascii = false; break; }

    QSqlQuery q(m_db);
    if (ascii) {
        q.prepare(QStringLiteral(
            "SELECT id, title, artist, album FROM tracks"
            " WHERE title LIKE :f ESCAPE '\\' OR artist LIKE :f ESCAPE '\\'"
            " OR album LIKE :f ESCAPE '\\'"));
        q.bindValue(QStringLiteral(":f"), QStringLiteral("%%%1%%").arg(escapeLike(needle)));
    } else {
        q.prepare(QStringLiteral("SELECT id, title, artist, album FROM tracks"));
    }
    if (!q.exec()) return out;

    QList<QPair<int, qint64>> hits;
    while (q.next()) {
        const int s = MatchScore::score(q.value(1).toString(), q.value(2).toString(),
                                        q.value(3).toString(), needle);
        if (s >= 0) hits.append({s, q.value(0).toLongLong()});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });
    out[QStringLiteral("total")] = hits.size();

    // Only the rows that survived the cap are turned into maps.
    QVariantList rows;
    for (int i = 0; i < hits.size() && i < limit; ++i) {
        QSqlQuery r(m_db);
        r.prepare(QStringLiteral("SELECT * FROM tracks WHERE id = :id"));
        r.bindValue(QStringLiteral(":id"), hits[i].second);
        if (r.exec() && r.next()) {
            QVariantMap m = rowToMap(r);
            m[QStringLiteral("_score")] = hits[i].first;
            rows.append(m);
        }
    }
    out[QStringLiteral("rows")] = rows;
    return out;
}

QVariantMap LocalLibrary::searchPlaylists(const QString &query, int limit) const {
    QVariantMap out;
    out[QStringLiteral("rows")]  = QVariantList();
    out[QStringLiteral("total")] = 0;

    const QString needle = query.trimmed();
    if (needle.isEmpty() || limit <= 0) return out;

    // Local playlists are a handful, so the whole list is scored in place.
    QList<QPair<int, QVariantMap>> scored;
    for (const QVariant &v : playlists()) {
        QVariantMap m = v.toMap();
        const int s = MatchScore::score(m.value(QStringLiteral("title")).toString(), needle);
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
        if (fi.isDir() || isCue(fi)) return true;
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
    resolveTools();
    if (m_ffprobe.isEmpty()) {
        emit error(tr("ffprobe was not found — run “brew install ffmpeg” to import local files."));
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
        } else if (isCue(fi)) {
            // A dropped sheet stands for the audio it describes, and is used
            // for it even when its name does not match the audio file's.
            for (const QString &audio : audioFilesFor(fi.absoluteFilePath())) {
                files << audio;
                m_cueFor.insert(audio, fi.absoluteFilePath());
            }
        } else if (supportedSuffixes().contains(fi.suffix().toLower())) {
            files << fi.absoluteFilePath();
        }
    }
    files.removeDuplicates();

    if (files.isEmpty()) {
        m_cueFor.clear();
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
    // the UNIQUE constraint. Two exceptions: an album imported whole before
    // its CUE sheet was read is split, and a CUE album missing tracks the
    // user removed gets them back.
    QSqlQuery dup(m_db);
    dup.prepare(QStringLiteral("SELECT COUNT(*), MAX(start_ms), MAX(end_ms) FROM tracks WHERE path = :p"));
    dup.bindValue(QStringLiteral(":p"), path);
    const bool imported = dup.exec() && dup.next() && dup.value(0).toInt() > 0;
    const bool whole = imported && dup.value(1).toLongLong() == 0 && dup.value(2).toLongLong() == 0;
    if (imported && whole && cueTracksFor(path).size() < 2) {
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
                           QStringLiteral("-show_chapters"),
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

            // One row per track: a single file normally, one per CUE entry for
            // an album ripped to a single file.
            const qint64 fileMs = qRound64(format.value(QStringLiteral("duration")).toString().toDouble() * 1000);
            QList<Slice> slices = slicesFor(path, tags, root.value(QStringLiteral("chapters")).toArray(), fileMs);
            const bool split = !slices.isEmpty();
            if (!split) slices.append(Slice{});

            const QFileInfo fi(path);
            QString albumArtist = tag(tags, {QStringLiteral("album_artist"),
                                             QStringLiteral("albumartist")});
            if (albumArtist.isEmpty()) albumArtist = tag(tags, {QStringLiteral("artist")});
            const qint64 now = QDateTime::currentMSecsSinceEpoch();

            m_db.transaction();
            // Replaces a whole-album row imported before its sheet was read.
            // Its playlist entries go with it; there is no one track to
            // hand them to.
            bool dropped = false;
            if (split) {
                QSqlQuery drop(m_db);
                drop.prepare(QStringLiteral("DELETE FROM tracks WHERE path = :p AND start_ms = 0 AND end_ms = 0"));
                drop.bindValue(QStringLiteral(":p"), path);
                dropped = drop.exec() && drop.numRowsAffected() > 0;
            }
            // Tracks of this file still in the library keep their rows, and
            // with them their playlist entries.
            QSet<qint64> present;
            QSqlQuery have(m_db);
            have.prepare(QStringLiteral("SELECT start_ms FROM tracks WHERE path = :p"));
            have.bindValue(QStringLiteral(":p"), path);
            if (have.exec()) while (have.next()) present.insert(have.value(0).toLongLong());
            if (!split && !present.isEmpty()) slices.clear();   // a whole file already there

            QList<qint64> rowIds;
            bool ok = true;
            for (const Slice &sl : std::as_const(slices)) {
                if (present.contains(sl.startMs)) continue;
                QString title = sl.title.isEmpty() ? tag(tags, {QStringLiteral("title")}) : sl.title;
                if (title.isEmpty()) title = split ? tr("Track %1").arg(sl.number)
                                                   : fi.completeBaseName();   // untagged file
                const qint64 endMs = sl.endMs > 0 ? sl.endMs : fileMs;

                QSqlQuery ins(m_db);
                ins.prepare(QStringLiteral(
                    "INSERT INTO tracks (path,title,artist,album,album_artist,year,codec,duration,track_no,disc_no,added_at,start_ms,end_ms)"
                    " VALUES (:path,:title,:artist,:album,:album_artist,:year,:codec,:duration,:track_no,:disc_no,:added,:start,:end)"));
                ins.bindValue(QStringLiteral(":path"),  path);
                ins.bindValue(QStringLiteral(":title"), title);
                // A sheet's own PERFORMER lines outrank the file's tags: the
                // file tag names the album, the sheet names each track.
                QString artist = !sl.performer.isEmpty() ? sl.performer : sl.albumArtist;
                if (artist.isEmpty())
                    artist = tag(tags, {QStringLiteral("artist"), QStringLiteral("album_artist")});
                ins.bindValue(QStringLiteral(":artist"), artist);
                ins.bindValue(QStringLiteral(":album"),
                              !sl.album.isEmpty() ? sl.album : tag(tags, {QStringLiteral("album")}));
                // Falls back to the performer so the column is never empty: the
                // ordering reads it directly and a null would sort to the top.
                ins.bindValue(QStringLiteral(":album_artist"),
                              !sl.albumArtist.isEmpty() ? sl.albumArtist : albumArtist);
                ins.bindValue(QStringLiteral(":year"), !sl.year.isEmpty() ? sl.year
                              : tag(tags, {QStringLiteral("date"), QStringLiteral("year")}));
                ins.bindValue(QStringLiteral(":codec"), codec);
                ins.bindValue(QStringLiteral(":duration"),
                              static_cast<int>(qRound64((endMs - sl.startMs) / 1000.0)));
                ins.bindValue(QStringLiteral(":track_no"),
                              split ? sl.number : tagNumber(tags, {QStringLiteral("track")}));
                ins.bindValue(QStringLiteral(":disc_no"),
                              qMax(1, sl.disc > 0 ? sl.disc : tagNumber(tags, {QStringLiteral("disc")})));
                ins.bindValue(QStringLiteral(":added"), now);
                ins.bindValue(QStringLiteral(":start"), sl.startMs);
                ins.bindValue(QStringLiteral(":end"),   sl.endMs);
                if (!ins.exec()) { ok = false; break; }
                rowIds << ins.lastInsertId().toLongLong();
            }

            if (ok && rowIds.isEmpty()) {
                m_db.commit();
                ++m_skipped;
            } else if (ok) {
                m_db.commit();
                if (dropped) { renumberPlaylists(); emit playlistsChanged(); }
                m_added += rowIds.size();
                // Embedded art wins: it is per-track, so it stays correct on
                // compilations where one folder image would not be. The
                // tracks of one file share one extracted image.
                QString cover = hasCoverStream ? extractCover(path, rowIds.first()) : QString();
                if (cover.isEmpty()) cover = findFolderCover(path);
                for (qint64 rowId : std::as_const(rowIds)) {
                    if (cover.isEmpty()) break;
                    QSqlQuery up(m_db);
                    up.prepare(QStringLiteral("UPDATE tracks SET cover = :c WHERE id = :id"));
                    up.bindValue(QStringLiteral(":c"), cover);
                    up.bindValue(QStringLiteral(":id"), rowId);
                    up.exec();
                }
            } else {
                m_db.rollback();
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

// ─── CUE sheets ────────────────────────────────────────

// The sheet for an album ripped to one file: one dropped alongside it wins,
// then the names rippers use (album.cue, album.flac.cue), then any sheet in
// the folder that names the file.
QString LocalLibrary::findCue(const QString &audioPath) const {
    const QString given = m_cueFor.value(audioPath);
    if (!given.isEmpty()) return given;

    const QFileInfo audio(audioPath);
    const QDir dir = audio.absoluteDir();
    QString byName;
    const QFileInfoList sheets = dir.entryInfoList({QStringLiteral("*.cue"), QStringLiteral("*.CUE")},
                                                   QDir::Files, QDir::Name);
    for (const QFileInfo &f : sheets) {
        const QString stem = f.completeBaseName();
        if (stem.compare(audio.completeBaseName(), Qt::CaseInsensitive) == 0
            || stem.compare(audio.fileName(), Qt::CaseInsensitive) == 0)
            return f.absoluteFilePath();
    }
    for (const QFileInfo &f : sheets)
        if (!CueSheet::load(f.absoluteFilePath()).tracksFor(audioPath).isEmpty())
            return f.absoluteFilePath();
    return {};
}

QList<CueSheet::Track> LocalLibrary::cueTracksFor(const QString &audioPath) const {
    const QString cue = findCue(audioPath);
    return cue.isEmpty() ? QList<CueSheet::Track>() : CueSheet::load(cue).tracksFor(audioPath);
}

// The audio files a sheet describes, as they exist on disk. A FILE entry that
// names a missing album.wav still finds album.flac: rippers encode after
// writing the sheet and rarely fix it up.
QStringList LocalLibrary::audioFilesFor(const QString &cuePath) const {
    const QDir dir = QFileInfo(cuePath).absoluteDir();
    QStringList out;
    for (const CueSheet::Track &t : CueSheet::load(cuePath).tracks) {
        const QString name = QFileInfo(QString(t.file).replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName();
        if (name.isEmpty()) continue;
        QString found;
        if (QFileInfo::exists(dir.filePath(name))
            && supportedSuffixes().contains(QFileInfo(name).suffix().toLower())) {
            found = dir.filePath(name);
        } else {
            const QString stem = QFileInfo(name).completeBaseName();
            for (const QString &suffix : supportedSuffixes()) {
                const QString candidate = dir.filePath(stem + QLatin1Char('.') + suffix);
                if (QFileInfo::exists(candidate)) { found = candidate; break; }
            }
        }
        if (!found.isEmpty() && !out.contains(found)) out << QFileInfo(found).absoluteFilePath();
    }
    return out;
}

// The tracks inside one file, or nothing when it holds a single track.
// Sources in order: a .cue beside it, a sheet embedded in its tags (the
// CUESHEET Vorbis comment), and a FLAC's own chapter list.
QList<LocalLibrary::Slice> LocalLibrary::slicesFor(const QString &path, const QJsonObject &tags,
                                                   const QJsonArray &chapters, qint64 fileMs) const {
    QList<Slice> out;

    CueSheet sheet;
    QList<CueSheet::Track> cue;
    const QString cuePath = findCue(path);
    if (!cuePath.isEmpty()) {
        sheet = CueSheet::load(cuePath);
        cue = sheet.tracksFor(path);
    }
    if (cue.size() < 2) {
        const QString embedded = tag(tags, {QStringLiteral("cuesheet")});
        if (!embedded.isEmpty()) {
            sheet = CueSheet::parse(embedded);
            // An embedded sheet describes this file, whatever FILE it names
            // (usually the "CDImage.wav" it was ripped to).
            cue = sheet.tracks;
        }
    }
    if (cue.size() >= 2) {
        for (const CueSheet::Track &t : std::as_const(cue)) {
            Slice s;
            s.number      = t.number;
            s.title       = t.title;
            s.performer   = t.performer;
            s.album       = sheet.title;
            s.albumArtist = sheet.performer;
            s.year        = sheet.date;
            s.disc        = sheet.disc;
            s.startMs     = t.startMs;
            out << s;
        }
    } else if (QFileInfo(path).suffix().compare(QLatin1String("flac"), Qt::CaseInsensitive) == 0
               && chapters.size() >= 2) {
        // Only for FLAC: chapters in an M4A are an audiobook or a podcast,
        // which the user wants as one item.
        int n = 0;
        for (const QJsonValue &v : chapters) {
            const QJsonObject c = v.toObject();
            Slice s;
            s.number  = ++n;
            s.title   = tag(c.value(QStringLiteral("tags")).toObject(), {QStringLiteral("title")});
            s.startMs = qRound64(c.value(QStringLiteral("start_time")).toString().toDouble() * 1000);
            out << s;
        }
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const Slice &a, const Slice &b) { return a.startMs < b.startMs; });
    // A sheet for a different edit of the album can run past the end of this
    // file; those entries would play nothing.
    if (fileMs > 0)
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [fileMs](const Slice &s) { return s.startMs >= fileMs; }),
                  out.end());
    for (int i = 0; i + 1 < out.size(); ++i) out[i].endMs = out[i + 1].startMs;
    // Two entries at one offset would collide on UNIQUE(path, start_ms).
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const Slice &s) { return s.endMs > 0 && s.endMs <= s.startMs; }),
              out.end());
    if (out.size() < 2) out.clear();
    return out;
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
    m_cueFor.clear();
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
        const QString c = (cover.exec() && cover.next()) ? cover.value(0).toString() : QString();
        QSqlQuery del(m_db);
        del.prepare(QStringLiteral("DELETE FROM tracks WHERE id = :id"));
        del.bindValue(QStringLiteral(":id"), id);
        del.exec();
        // Only art we extracted is ours to delete, and only once no other
        // track uses it: the tracks of a CUE album share one image, and a
        // folder cover is the user's own file.
        if (!c.isEmpty() && QFileInfo(c).absolutePath() == QFileInfo(m_coverDir).absoluteFilePath()) {
            QSqlQuery used(m_db);
            used.prepare(QStringLiteral("SELECT 1 FROM tracks WHERE cover = :c LIMIT 1"));
            used.bindValue(QStringLiteral(":c"), c);
            if (used.exec() && !used.next()) QFile::remove(c);
        }
    }
    m_db.commit();
    renumberPlaylists();
    emit tracksChanged();
    emit playlistsChanged();
}

// The cascade from a deleted track leaves gaps in the position sequence of any
// playlist that held it, and positions are what the pages pass back when
// removing or moving an item, so renumber every playlist.
void LocalLibrary::renumberPlaylists() {
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
