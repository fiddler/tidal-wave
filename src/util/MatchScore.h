#pragma once
#include <QString>

// Ranking shared by every command-palette source, so a liked album and a local
// file are ordered by the same rule no matter which class produced them.
//
// Higher is better, -1 means no match. `primary` is the name a user would type
// (track/album/playlist title, artist name); the secondaries are fields worth
// searching but not worth ranking on (a track's artist and album). Any primary
// hit outranks every secondary hit, which is why the primary score is scaled:
// otherwise a song whose *album* is called "Blue" would sit above the song
// actually called "Blue".
//
// Case folding is Qt's, not the caller's and not SQLite's: it is the only one
// of the three that is right for "Ä" vs "ä". Nothing here lowercases a string,
// so scoring thousands of rows on every keystroke allocates nothing.
namespace MatchScore {

inline int fieldScore(const QString &field, const QString &query) {
    if (field.isEmpty() || query.isEmpty()) return -1;
    if (field.compare(query, Qt::CaseInsensitive) == 0)   return 100;
    if (field.startsWith(query, Qt::CaseInsensitive))     return 80;
    int at = field.indexOf(query, 0, Qt::CaseInsensitive);
    if (at < 0) return -1;
    // A hit that starts a word ("side" in "Dark Side of the Moon") reads as a
    // real match; one buried mid-word ("ide") is noise and ranks last.
    while (at > 0) {
        if (!field.at(at - 1).isLetterOrNumber()) return 60;
        at = field.indexOf(query, at + 1, Qt::CaseInsensitive);
    }
    return 40;
}

inline int score(const QString &primary, const QString &query) {
    const int p = fieldScore(primary, query);
    return p >= 0 ? p * 10 : -1;
}

inline int score(const QString &primary, const QString &secondary1,
                 const QString &secondary2, const QString &query) {
    const int p = fieldScore(primary, query);
    if (p >= 0) return p * 10;
    return qMax(fieldScore(secondary1, query), fieldScore(secondary2, query));
}

} // namespace MatchScore
