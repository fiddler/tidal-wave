#pragma once
#include <QList>
#include <QString>

// A CUE sheet: one audio file (typically a whole-album FLAC rip) described as
// a list of tracks by start offset. The library imports each track as its own
// row playing a slice of the file, so nothing is ever split on disk.
struct CueSheet {
    struct Track {
        QString file;        // FILE entry the track sits in, as written in the sheet
        int     number = 0;
        QString title;
        QString performer;
        qint64  startMs = 0; // INDEX 01
    };

    QString title;           // album
    QString performer;       // album artist
    QString date;
    int     disc = 0;
    QList<Track> tracks;

    // Accepts UTF-8 (with or without BOM) and falls back to Windows-1252 for
    // the legacy sheets EAC and friends wrote on Windows.
    static CueSheet parse(const QByteArray &data);
    static CueSheet parse(const QString &text);
    static CueSheet load(const QString &path);

    // Tracks whose FILE entry names this audio file. A match on the base name
    // alone is accepted too: rippers rename album.wav to album.flac after
    // encoding without touching the sheet.
    QList<Track> tracksFor(const QString &audioPath) const;
};
