// Response parsing and the small pure rules around it. No network: every case
// feeds a fixture shaped like the reply it stands for.
#include <QtTest>
#include <QJsonDocument>
#include "TidalClient.h"
#include "TidalBridge.h"
#include "QueueModel.h"
#include <QSignalSpy>

using namespace Tidal;

// Fixtures are written with single quotes and turned into JSON here: moc reads
// this file, and it loses track of the class across a raw string literal.
static QByteArray json(const char *text) {
    return QByteArray(text).replace('\'', '"');
}
static QJsonObject obj(const char *text) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json(text), &err);
    if (err.error != QJsonParseError::NoError) qFatal("bad fixture: %s", qPrintable(err.errorString()));
    return doc.object();
}

static Album album(qint64 id, const QString &title) { Album a; a.id = id; a.title = title; return a; }
static Mix   mix(const QString &id, qint64 addedAt = 0) { Mix m; m.id = id; m.addedAt = addedAt; return m; }

static Playlist playlist(const QString &uuid, qint64 addedAt = 0) {
    Playlist p; p.uuid = uuid; p.addedAt = addedAt; return p;
}
static QList<QueueModel::Row> rows(const QList<int> &ids) {
    QList<QueueModel::Row> out;
    for (int i = 0; i < ids.size(); ++i) {
        QVariantMap t; t["id"] = ids[i];
        out.append({ t, i });
    }
    return out;
}
static QList<int> ids(const QueueModel &m) {
    QList<int> out;
    for (int i = 0; i < m.rowCount(); ++i)
        out << m.data(m.index(i), QueueModel::TrackRole).toMap().value("id").toInt();
    return out;
}

class TstApi : public QObject {
    Q_OBJECT
private slots:
    // ── discography ────────────────────────────────
    void albumListsMergeOnIdAndKeepTheFirstPosition() {
        const auto out = TidalClient::mergeAlbumLists({
            { album(1, "Album"), album(2, "Other") },
            { album(3, "Single"), album(1, "Album again") },
            { album(4, "Compilation") } });
        QCOMPARE(out.size(), 4);
        QCOMPARE(out[0].title, QStringLiteral("Album"));
        QCOMPARE(out[2].id, 3);
        QCOMPARE(out[3].id, 4);
    }
    void albumsWithoutAnIdAreNotCollapsedIntoOne() {
        const auto out = TidalClient::mergeAlbumLists({ { album(0, "a"), album(0, "b") } });
        QCOMPARE(out.size(), 2);
    }

    // ── mixes ──────────────────────────────────────
    void generatedMixListDropsVideoMixes() {
        const auto root = obj("{'rows':[{'modules':["
            "{'type':'OTHER','pagedList':{'items':[{'id':'x'}]}},"
            "{'type':'MIX_LIST','pagedList':{'items':["
            "{'id':'a','title':'My Mix 1','subTitle':'s','mixType':'DAILY_MIX',"
            "'images':{'LARGE':{'url':'https://img/large.jpg'}}},"
            "{'id':'v','title':'My Video Mix 1','mixType':'VIDEO_DAILY_MIX'}]}}]}]}");
        const auto mixes = TidalClient::parseMixPage(root);
        QCOMPARE(mixes.size(), 1);
        QCOMPARE(mixes[0].id, QStringLiteral("a"));
        QCOMPARE(mixes[0].subTitle, QStringLiteral("s"));
        QCOMPARE(mixes[0].coverUrl(), QStringLiteral("https://img/large.jpg"));
    }
    void savedMixListKeepsEverythingTheUserSaved() {
        const auto root = obj("{'items':["
            "{'id':'r','title':'Song Radio','mixType':'TRACK_MIX',"
            "'dateAdded':'2026-06-08T19:33:03.142667Z',"
            "'detailImages':{'MEDIUM':{'url':'https://img/m.jpg'}}},"
            "{'id':'v','title':'Video','mixType':'VIDEO_DAILY_MIX'},"
            "{'title':'no id'}],'cursor':null}");
        const auto mixes = TidalClient::parseSavedMixes(root);
        QCOMPARE(mixes.size(), 2);
        QVERIFY(mixes[0].isTrackMix());
        QVERIFY(mixes[0].addedAt > 0);
        QCOMPARE(mixes[0].coverUrl(), QStringLiteral("https://img/m.jpg"));
        QVERIFY(mixes[1].isVideoMix());
        QCOMPARE(mixes[1].addedAt, 0);
    }
    void cursorPagingEnds_data() {
        QTest::addColumn<QByteArray>("page");
        QTest::addColumn<QString>("previous");
        QTest::addColumn<QString>("expected");
        QTest::newRow("next page")     << json("{'items':[{'id':'a'}],'cursor':'c2'}") << "c1" << "c2";
        QTest::newRow("null cursor")   << json("{'items':[{'id':'a'}],'cursor':null}") << "c1" << "";
        QTest::newRow("no cursor key") << json("{'items':[{'id':'a'}]}")               << ""   << "";
        QTest::newRow("empty page")    << json("{'items':[],'cursor':'c2'}")           << "c1" << "";
        // The same cursor again would fetch the same page for ever.
        QTest::newRow("same cursor")   << json("{'items':[{'id':'a'}],'cursor':'c1'}") << "c1" << "";
    }
    void cursorPagingEnds() {
        QFETCH(QByteArray, page);
        QFETCH(QString, previous);
        QFETCH(QString, expected);
        QCOMPARE(TidalClient::nextCursor(QJsonDocument::fromJson(page).object(), previous), expected);
    }
    void mixListsMergeOnIdAndAdoptTheSavedDate() {
        const auto out = TidalClient::mergeMixLists({
            { mix("a"), mix("b") },
            { mix("c", 50), mix("a", 99) } });
        QCOMPARE(out.size(), 3);
        QCOMPARE(out[0].id, QStringLiteral("a"));     // the generated list fixes the order
        QCOMPARE(out[0].addedAt, 99);                 // only the saved record has the date
        QCOMPARE(out[2].id, QStringLiteral("c"));
    }
    void mixPageYieldsHeaderAndTracks() {
        const auto root = obj("{'rows':["
            "{'modules':[{'type':'MIX_HEADER','mix':{'id':'m','title':'Song Radio','subTitle':'Artist',"
            "'mixType':'TRACK_MIX'}}]},"
            "{'modules':[{'type':'VIDEO_LIST','pagedList':{'items':[{'id':9}]}},"
            "{'type':'TRACK_LIST','pagedList':{'items':["
            "{'id':1,'title':'One','artists':[{'id':5,'name':'A'},{'id':6,'name':'B'}]},"
            "{'item':{'id':2,'title':'Two'}}]}}]}]}");
        const Mix header = TidalClient::parseMixHeader(root);
        QCOMPARE(header.title, QStringLiteral("Song Radio"));
        QVERIFY(header.isTrackMix());
        const auto tracks = TidalClient::parseMixTracks(root);
        QCOMPARE(tracks.size(), 2);                   // the VIDEO_LIST is not read
        QCOMPARE(tracks[0].artists.size(), 2);
        QCOMPARE(tracks[1].id, 2);
    }
    void aSavedMixIsOnlySavedWhenTheReplyNamesIt() {
        const auto added = obj("{'addedItems':['m1'],'deletedItems':[]}");
        QVERIFY( TidalClient::mixFavoriteAccepted(added, "m1", true));
        QVERIFY(!TidalClient::mixFavoriteAccepted(added, "m2", true));
        QVERIFY(!TidalClient::mixFavoriteAccepted(added, "m1", false));
        QVERIFY(!TidalClient::mixFavoriteAccepted(QJsonObject(), "m1", true));
        QVERIFY(!TidalClient::mixFavoriteAccepted(added, QString(), true));
    }
    void trackCarriesItsRadioStation() {
        const Track t = Track::fromJson(obj("{'id':7,'title':'T',"
            "'mixes':{'TRACK_MIX':'001603cb31f1842e052770e0ad7647','MASTER_TRACK_MIX':'x'}}"));
        QCOMPARE(t.trackMixId, QStringLiteral("001603cb31f1842e052770e0ad7647"));
        QVERIFY(Track::fromJson(obj("{'id':8}")).trackMixId.isEmpty());
    }

    // ── search ─────────────────────────────────────
    void searchReplyCarriesEveryKindAndItsTotal() {
        const auto r = TidalClient::parseSearch(obj("{"
            "'tracks':{'items':[{'id':1,'title':'T'}],'totalNumberOfItems':120},"
            "'albums':{'items':[{'id':2,'title':'A'}],'totalNumberOfItems':30},"
            "'artists':{'items':[{'id':3,'name':'N'}],'totalNumberOfItems':4},"
            "'playlists':{'items':[{'uuid':'u','title':'P'}],'totalNumberOfItems':9},"
            "'mixes':{'items':[{'id':'m','title':'M','mixType':'ARTIST_MIX'},"
            "{'id':'v','title':'V','mixType':'VIDEO_DAILY_MIX'},"
            "{'title':'no id'}],'totalNumberOfItems':3}}"));
        QCOMPARE(r.tracks.size(), 1);     QCOMPARE(r.totalTracks, 120);
        QCOMPARE(r.albums.size(), 1);     QCOMPARE(r.totalAlbums, 30);
        QCOMPARE(r.artists.size(), 1);    QCOMPARE(r.totalArtists, 4);
        QCOMPARE(r.playlists.size(), 1);  QCOMPARE(r.totalPlaylists, 9);
        QCOMPARE(r.mixes.size(), 1);      QCOMPARE(r.totalMixes, 3);
        QCOMPARE(r.mixes[0].id, QStringLiteral("m"));
    }
    void aSingleKindPageLeavesTheOthersEmpty() {
        const auto r = TidalClient::parseSearch(obj(
            "{'albums':{'items':[{'id':2,'title':'A'}],'totalNumberOfItems':30}}"));
        QCOMPARE(r.albums.size(), 1);
        QVERIFY(r.tracks.isEmpty());
        QCOMPARE(r.totalTracks, 0);
    }
    void recentSearches_data() {
        QTest::addColumn<QStringList>("before");
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("after");
        QTest::newRow("first")              << QStringList{}                 << "gru"         << QStringList{"gru"};
        QTest::newRow("typing on")          << QStringList{"gru", "abba"}    << "grube"       << QStringList{"grube", "abba"};
        QTest::newRow("backspacing")        << QStringList{"abba", "grube"}  << "gru"         << QStringList{"grube", "abba"};
        QTest::newRow("case")               << QStringList{"Radiohead"}      << "radio"       << QStringList{"Radiohead"};
        QTest::newRow("unrelated")          << QStringList{"abba"}           << "bjork"       << QStringList{"bjork", "abba"};
        QTest::newRow("repeat moves up")    << QStringList{"abba", "bjork"}  << "bjork"       << QStringList{"bjork", "abba"};
        QTest::newRow("trimmed")            << QStringList{"abba"}           << "  bjork "    << QStringList{"bjork", "abba"};
        QTest::newRow("blank is ignored")   << QStringList{"abba"}           << "   "         << QStringList{"abba"};
    }
    void recentSearches() {
        QFETCH(QStringList, before);
        QFETCH(QString, query);
        QFETCH(QStringList, after);
        QCOMPARE(TidalBridge::mergeRecentSearch(before, query), after);
    }
    void recentSearchesAreCapped() {
        QStringList list;
        for (int i = 0; i < 12; ++i)
            list = TidalBridge::mergeRecentSearch(list, QStringLiteral("q%1x").arg(QChar('a' + i)));
        QCOMPARE(list.size(), 8);
        QCOMPARE(list.first(), QStringLiteral("qlx"));
    }

    // ── credits ────────────────────────────────────
    void creditsKeepRolesThatNameSomeone() {
        const auto doc = QJsonDocument::fromJson(json("["
            "{'type':'Producer','contributors':[{'name':'Nigel','id':11},{'name':'Studio Hand'}]},"
            "{'type':'Engineer','contributors':[]},"
            "{'type':'','contributors':[{'name':'Nobody','id':1}]},"
            "{'type':'Mixer','contributors':[{'id':3}]}]"));
        const auto groups = TidalClient::parseCredits(doc.array());
        QCOMPARE(groups.size(), 1);
        QCOMPARE(groups[0].type, QStringLiteral("Producer"));
        QCOMPARE(groups[0].contributors.size(), 2);
        QCOMPARE(groups[0].contributors[0].id, 11);
        QCOMPARE(groups[0].contributors[1].id, 0);    // a name with no artist page
    }

    // ── playlist order ─────────────────────────────
    void playlistsOrderByTheLatestOfPlayedAndSaved() {
        QList<Playlist> lists{ playlist("old", 100), playlist("played", 50),
                               playlist("new", 900), playlist("never") };
        QVariantMap playtimes;
        playtimes["played"] = 500LL;
        playtimes["old"]    = 10LL;       // played before it was saved: the save wins
        TidalBridge::sortByRecency(lists, playtimes);
        QStringList order;
        for (const auto &p : lists) order << p.uuid;
        QCOMPARE(order, (QStringList{ "new", "played", "old", "never" }));
    }
    // A save made while the saved-mix pages were in flight used to be undone
    // by the older snapshot landing after it.
    void mixEditsMadeDuringALoadSurviveIt() {
        QHash<QString, bool> edits;
        edits["saved-meanwhile"]   = true;
        edits["unsaved-meanwhile"] = false;
        const auto ids = TidalBridge::applyMixEdits({ "kept", "unsaved-meanwhile" }, edits);
        QCOMPARE(ids, (QSet<QString>{ "kept", "saved-meanwhile" }));
    }
    void playlistTakesItsDateFromTheCollectionRow() {
        const auto lists = TidalClient::parsePlaylists(obj(
            "{'items':[{'type':'USER_FAVORITE','created':'2026-09-01T10:00:00.000+0000',"
            "'playlist':{'uuid':'u1','title':'Saved','created':'2019-01-01T00:00:00.000+0000'}},"
            "{'uuid':'u2','title':'Bare','created':'2019-01-01T00:00:00.000+0000'}]}"));
        QCOMPARE(lists.size(), 2);
        QCOMPARE(lists[0].addedAt, QDateTime::fromString("2026-09-01T10:00:00Z", Qt::ISODate).toMSecsSinceEpoch());
        QCOMPARE(lists[1].addedAt, 0);    // no row, no acquisition date
    }

    // ── queue model ────────────────────────────────
    void queueModelReportsAMoveAsAMove_data() {
        QTest::addColumn<QList<int>>("before");
        QTest::addColumn<QList<int>>("after");
        QTest::newRow("down one")  << QList<int>{1,2,3,4,5} << QList<int>{1,3,2,4,5};
        QTest::newRow("down far")  << QList<int>{1,2,3,4,5} << QList<int>{2,3,4,5,1};
        QTest::newRow("up far")    << QList<int>{1,2,3,4,5} << QList<int>{5,1,2,3,4};
        QTest::newRow("up middle") << QList<int>{1,2,3,4,5} << QList<int>{1,4,2,3,5};
    }
    void queueModelReportsAMoveAsAMove() {
        QFETCH(QList<int>, before);
        QFETCH(QList<int>, after);
        QueueModel m;
        m.sync(rows(before));
        QSignalSpy moved(&m, &QAbstractItemModel::rowsMoved);
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
        m.sync(rows(after));
        QCOMPARE(ids(m), after);
        QCOMPARE(moved.count(), 1);
        QCOMPARE(reset.count(), 0);
    }
    void queueModelRemovesAndInsertsInPlace() {
        QueueModel m;
        m.sync(rows({1,2,3,4,5}));
        QSignalSpy removed(&m, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&m, &QAbstractItemModel::rowsInserted);
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);

        m.sync(rows({1,2,4,5}));
        QCOMPARE(ids(m), (QList<int>{1,2,4,5}));
        QCOMPARE(removed.count(), 1);
        QCOMPARE(removed[0][1].toInt(), 2);

        m.sync(rows({1,9,8,2,4,5}));                 // "play next" with two tracks
        QCOMPARE(ids(m), (QList<int>{1,9,8,2,4,5}));
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(inserted[0][1].toInt(), 1);
        QCOMPARE(inserted[0][2].toInt(), 2);
        QCOMPARE(reset.count(), 0);
        // Every row's queue index follows the edit, not only the edited rows.
        QCOMPARE(m.data(m.index(5), QueueModel::QueueIndexRole).toInt(), 5);
    }
    void queueModelLeavesAnUnchangedQueueAlone() {
        // The track changing emits queueChanged with the same tracks: no row
        // may be rebuilt, or the panel jumps back to the top on every song.
        QueueModel m;
        m.sync(rows({1,2,3}));
        QSignalSpy any(&m, &QAbstractItemModel::modelReset);
        QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
        m.sync(rows({1,2,3}));
        QCOMPARE(any.count(), 0);
        QCOMPARE(changed.count(), 0);
    }
    void queueModelResetsForAnythingElse() {
        QueueModel m;
        m.sync(rows({1,2,3,4}));
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
        m.sync(rows({4,3,2,1}));                     // a shuffle
        QCOMPARE(ids(m), (QList<int>{4,3,2,1}));
        QCOMPARE(reset.count(), 1);
        m.sync(rows({7,8}));                         // a new queue
        QCOMPARE(ids(m), (QList<int>{7,8}));
        m.sync({});
        QCOMPARE(m.rowCount(), 0);
    }
    void queueModelCopesWithTheSameTrackTwice() {
        QueueModel m;
        m.sync(rows({1,1,2,1}));
        m.sync(rows({1,2,1,1}));
        QCOMPARE(ids(m), (QList<int>{1,2,1,1}));
        m.sync(rows({1,2,1}));
        QCOMPARE(ids(m), (QList<int>{1,2,1}));
    }

    // ── favourites wrappers ────────────────────────
    void favouriteRowsAreUnwrapped() {
        const auto albums = TidalClient::parseAlbums(obj(
            "{'items':[{'created':'2026-01-01','item':{'id':5,'title':'Saved'}},{'id':6,'title':'Bare'}]}"));
        QCOMPARE(albums.size(), 2);
        QCOMPARE(albums[0].title, QStringLiteral("Saved"));
        const auto lists = TidalClient::parsePlaylists(obj(
            "{'items':[{'type':'USER_CREATED','playlist':{'uuid':'u1','title':'Mine'}}]}"));
        QCOMPARE(lists.size(), 1);
        QCOMPARE(lists[0].uuid, QStringLiteral("u1"));
    }
};

QTEST_GUILESS_MAIN(TstApi)
#include "tst_api.moc"
