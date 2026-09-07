#include "PlaybackDiagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

constexpr qint64 kMaxLogBytes = 10 * 1024 * 1024;
QMutex logMutex;

QString scrubString(QString value) {
    static const QRegularExpression url(
        QStringLiteral(R"((?:https?|file|tidalstream)://[^\s\"']+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bearer(
        QStringLiteral(R"((Bearer\s+)[A-Za-z0-9._~+/=-]+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression secret(
        QStringLiteral(R"(((?:access_token|refresh_token|authorization|signature|token)=)[^&\s]+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression userPath(
        QStringLiteral(R"(/Users/[^/\s]+)"));

    value.replace(url, QStringLiteral("<redacted-url>"));
    value.replace(bearer, QStringLiteral("\\1<redacted>"));
    value.replace(secret, QStringLiteral("\\1<redacted>"));
    value.replace(userPath, QStringLiteral("/Users/<redacted-user>"));
    return value;
}

void rotateIfNeeded(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists() || info.size() < kMaxLogBytes) return;

    const QString previous = path + QStringLiteral(".1");
    if (QFile::exists(previous)) QFile::remove(previous);
    QFile::rename(path, previous);
}

} // namespace

QString PlaybackDiagnostics::logPath() {
#ifdef Q_OS_MACOS
    return QDir::homePath() + QStringLiteral("/Library/Logs/Tidal Wave/playback.jsonl");
#else
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/logs/playback.jsonl");
#endif
}

QVariant PlaybackDiagnostics::sanitize(const QVariant &value) {
    switch (value.metaType().id()) {
    case QMetaType::QString:
        return scrubString(value.toString());
    case QMetaType::QStringList: {
        QStringList clean;
        for (const QString &item : value.toStringList()) clean.append(scrubString(item));
        return clean;
    }
    case QMetaType::QVariantList: {
        QVariantList clean;
        for (const QVariant &item : value.toList()) clean.append(sanitize(item));
        return clean;
    }
    case QMetaType::QVariantMap: {
        QVariantMap clean;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            clean.insert(it.key(), sanitize(it.value()));
        return clean;
    }
    default:
        return value;
    }
}

void PlaybackDiagnostics::record(const QString &event, const QVariantMap &fields) {
    QMutexLocker locker(&logMutex);

    const QString path = logPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    rotateIfNeeded(path);

    QVariantMap entry;
    entry.insert(QStringLiteral("timestamp"),
                 QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    entry.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
    entry.insert(QStringLiteral("event"), scrubString(event));
    for (auto it = fields.cbegin(); it != fields.cend(); ++it)
        entry.insert(it.key(), sanitize(it.value()));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    file.write(QJsonDocument::fromVariant(entry).toJson(QJsonDocument::Compact));
    file.write("\n");
    file.flush();
}
