#include "CueSheet.h"

#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>

namespace {

// The rest of the line after the keyword, unquoted. Quotes are optional in
// the wild, and a value can itself contain spaces.
QString value(const QString &line, int keywordLength) {
    QString v = line.mid(keywordLength).trimmed();
    if (v.startsWith(QLatin1Char('"'))) {
        const int close = v.indexOf(QLatin1Char('"'), 1);
        v = close > 0 ? v.mid(1, close - 1) : v.mid(1);
    }
    return v;
}

// mm:ss:ff, where ff is CD frames (1/75 s). Minutes run past 59 on long rips.
qint64 msf(const QString &s) {
    const QStringList p = s.split(QLatin1Char(':'));
    if (p.size() != 3) return -1;
    return (p[0].toLongLong() * 60 + p[1].toLongLong()) * 1000 + p[2].toLongLong() * 1000 / 75;
}

bool startsWithWord(const QString &line, QLatin1StringView word) {
    return line.startsWith(word, Qt::CaseInsensitive)
        && (line.size() == word.size() || line.at(word.size()).isSpace());
}

} // namespace

CueSheet CueSheet::parse(const QByteArray &data) {
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8.decode(data);
    if (utf8.hasError()) {
        QStringDecoder cp1252("windows-1252");
        text = cp1252.isValid() ? QString(cp1252.decode(data)) : QString::fromLatin1(data);
    }
    return parse(text);
}

CueSheet CueSheet::parse(const QString &text) {
    CueSheet sheet;
    QString file;
    Track  *track = nullptr;
    bool    hasStart = false;

    const auto closeTrack = [&] {
        // A track with no INDEX 01 cannot be placed on the timeline.
        if (track && !hasStart) sheet.tracks.removeLast();
        track = nullptr;
    };

    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.startsWith(QChar(0xFEFF))) line = line.mid(1).trimmed();
        if (line.isEmpty()) continue;

        if (startsWithWord(line, QLatin1StringView("FILE"))) {
            closeTrack();
            // FILE "name" WAVE: drop the trailing type before unquoting.
            QString rest = line.mid(4).trimmed();
            if (rest.startsWith(QLatin1Char('"'))) {
                file = value(rest, 0);
            } else {
                const int sp = rest.lastIndexOf(QLatin1Char(' '));
                file = sp > 0 ? rest.left(sp) : rest;
            }
        } else if (startsWithWord(line, QLatin1StringView("TRACK"))) {
            closeTrack();
            const QStringList p = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            // Data tracks on enhanced CDs have no audio in the rip.
            if (p.size() >= 3 && p[2].compare(QLatin1String("AUDIO"), Qt::CaseInsensitive) != 0)
                continue;
            sheet.tracks.append(Track{});
            track = &sheet.tracks.last();
            track->file = file;
            track->number = p.size() >= 2 ? p[1].toInt() : sheet.tracks.size();
            hasStart = false;
        } else if (startsWithWord(line, QLatin1StringView("INDEX"))) {
            const QStringList p = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (track && p.size() >= 3 && p[1].toInt() == 1) {
                const qint64 ms = msf(p[2]);
                if (ms >= 0) { track->startMs = ms; hasStart = true; }
            }
        } else if (startsWithWord(line, QLatin1StringView("TITLE"))) {
            (track ? track->title : sheet.title) = value(line, 5);
        } else if (startsWithWord(line, QLatin1StringView("PERFORMER"))) {
            (track ? track->performer : sheet.performer) = value(line, 9);
        } else if (startsWithWord(line, QLatin1StringView("REM"))) {
            const QString rem = line.mid(3).trimmed();
            if (startsWithWord(rem, QLatin1StringView("DATE")))
                sheet.date = value(rem, 4);
            else if (startsWithWord(rem, QLatin1StringView("DISCNUMBER")))
                sheet.disc = value(rem, 10).section(QLatin1Char('/'), 0, 0).toInt();
        }
    }
    closeTrack();
    return sheet;
}

CueSheet CueSheet::load(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    // A real sheet is a few kilobytes; anything huge is not one.
    return parse(f.read(1 << 20));
}

QList<CueSheet::Track> CueSheet::tracksFor(const QString &audioPath) const {
    const QFileInfo audio(audioPath);
    const auto names = [](const QString &file) {
        // Sheets written on Windows use backslashes.
        return QFileInfo(QString(file).replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName();
    };

    QList<Track> exact, byStem;
    for (const Track &t : tracks) {
        const QString name = names(t.file);
        // An embedded sheet may name no file at all.
        if (name.isEmpty() || name.compare(audio.fileName(), Qt::CaseInsensitive) == 0)
            exact << t;
        else if (QFileInfo(name).completeBaseName().compare(audio.completeBaseName(),
                                                             Qt::CaseInsensitive) == 0)
            byStem << t;
    }
    return exact.isEmpty() ? byStem : exact;
}
