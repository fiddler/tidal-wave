#pragma once

#include <QString>
#include <QVariantMap>

// Small, always-on playback flight recorder. Records are JSON Lines and are
// flushed immediately so a forced app or coreaudiod restart does not erase the
// lead-up to a stall.
class PlaybackDiagnostics {
public:
    static QString logPath();
    static void record(const QString &event, const QVariantMap &fields = {});

private:
    static QVariant sanitize(const QVariant &value);
};
