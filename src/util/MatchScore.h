#pragma once
#include <QString>
#include <QStringList>

// Ranking shared by every command-palette source, so a liked album and a local
// file are ordered by the same rule no matter which class produced them.
//
// Higher is better, -1 means no match. `primary` is the name a user would type
// (track/album/playlist title, artist name); `secondaries` are the fields worth
// searching but not worth ranking on (a track's artist and album). Any primary
// hit outranks every secondary hit, which is why the primary score is scaled:
// otherwise a song whose *album* is called "Blue" would sit above the song
// actually called "Blue".
namespace MatchScore {

// Query must already be lowercased and trimmed by the caller — it is compared
// against thousands of rows per keystroke and lowercasing it once matters.
inline int fieldScore(const QString &field, const QString &loweredQuery) {
    if (field.isEmpty() || loweredQuery.isEmpty()) return -1;
    const QString f = field.toLower();
    if (f == loweredQuery)         return 100;
    if (f.startsWith(loweredQuery)) return 80;
    int at = f.indexOf(loweredQuery);
    if (at < 0) return -1;
    // A hit that starts a word ("side" in "Dark Side of the Moon") reads as a
    // real match; one buried mid-word ("ide") is noise and ranks last.
    while (at > 0) {
        if (!f.at(at - 1).isLetterOrNumber()) return 60;
        at = f.indexOf(loweredQuery, at + 1);
    }
    return 40;
}

inline int score(const QString &primary, const QStringList &secondaries,
                 const QString &loweredQuery) {
    const int p = fieldScore(primary, loweredQuery);
    if (p >= 0) return p * 10;
    int best = -1;
    for (const QString &s : secondaries)
        best = qMax(best, fieldScore(s, loweredQuery));
    return best;
}

} // namespace MatchScore
