#include "Equalizer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>

#include <algorithm>

#include "Player.h"

namespace {

// Classic 10-band graphic EQ centers: 32 Hz … 16 kHz, one octave apart —
// the same layout eqMac's Advanced tab uses.
constexpr int kFrequencies[Equalizer::BandCount] = {
    32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000,
};

const char *kSettingsEnabled    = "eq/enabled";
const char *kSettingsGains      = "eq/gains";
const char *kSettingsPreamp     = "eq/preamp";
const char *kSettingsAutoPreamp = "eq/autoPreamp";
const char *kSettingsActive     = "eq/activeProfile";
const char *kSettingsProfiles   = "eq/profiles";

QJsonArray gainsToJson(const QVector<double> &gains) {
    QJsonArray arr;
    for (double g : gains) arr.append(g);
    return arr;
}

QVector<double> gainsFromJson(const QJsonArray &arr) {
    QVector<double> gains(Equalizer::BandCount, 0.0);
    const int n = std::min<int>(arr.size(), Equalizer::BandCount);
    for (int i = 0; i < n; ++i)
        gains[i] = qBound(-Equalizer::GainLimit, arr[i].toDouble(),
                          Equalizer::GainLimit);
    return gains;
}

} // namespace

Equalizer::Equalizer(Player *player, QObject *parent)
    : QObject(parent), m_playerCtl(player), m_gains(BandCount, 0.0) {
    m_applyTimer = new QTimer(this);
    m_applyTimer->setSingleShot(true);
    m_applyTimer->setInterval(60);
    connect(m_applyTimer, &QTimer::timeout, this, &Equalizer::applyNow);

    load();
    // Player holds the string until libmpv is up, so this is safe pre-audio.
    applyNow();
}

void Equalizer::load() {
    QSettings s;
    m_enabled    = s.value(QLatin1String(kSettingsEnabled), false).toBool();
    m_preamp     = qBound(-GainLimit,
                          s.value(QLatin1String(kSettingsPreamp), 0.0).toDouble(),
                          GainLimit);
    m_autoPreamp = s.value(QLatin1String(kSettingsAutoPreamp), false).toBool();
    m_activeProfile = s.value(QLatin1String(kSettingsActive)).toString();

    const auto gainsDoc =
        QJsonDocument::fromJson(s.value(QLatin1String(kSettingsGains)).toByteArray());
    if (gainsDoc.isArray()) m_gains = gainsFromJson(gainsDoc.array());

    const auto profilesDoc =
        QJsonDocument::fromJson(s.value(QLatin1String(kSettingsProfiles)).toByteArray());
    if (profilesDoc.isArray()) {
        for (const auto &v : profilesDoc.array()) {
            const QJsonObject o = v.toObject();
            const QString name  = o.value(QLatin1String("name")).toString().trimmed();
            if (name.isEmpty()) continue;
            m_userProfiles.append({name,
                                   gainsFromJson(o.value(QLatin1String("gains")).toArray())});
        }
    }
}

void Equalizer::persist() const {
    QSettings s;
    s.setValue(QLatin1String(kSettingsEnabled),    m_enabled);
    s.setValue(QLatin1String(kSettingsPreamp),     m_preamp);
    s.setValue(QLatin1String(kSettingsAutoPreamp), m_autoPreamp);
    s.setValue(QLatin1String(kSettingsActive),     m_activeProfile);
    s.setValue(QLatin1String(kSettingsGains),
               QJsonDocument(gainsToJson(m_gains)).toJson(QJsonDocument::Compact));
}

void Equalizer::persistProfiles() const {
    QJsonArray arr;
    for (const Profile &p : m_userProfiles) {
        QJsonObject o;
        o.insert(QLatin1String("name"),  p.name);
        o.insert(QLatin1String("gains"), gainsToJson(p.gains));
        arr.append(o);
    }
    QSettings().setValue(QLatin1String(kSettingsProfiles),
                         QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QVariantList Equalizer::gains() const {
    QVariantList list;
    for (double g : m_gains) list.append(g);
    return list;
}

double Equalizer::preamp() const {
    return m_autoPreamp ? effectivePreamp() : m_preamp;
}

double Equalizer::effectivePreamp() const {
    if (!m_autoPreamp) return m_preamp;
    const double top = *std::max_element(m_gains.cbegin(), m_gains.cend());
    return top > 0 ? -top : 0.0;
}

QVariantList Equalizer::profiles() const {
    QVariantList list;
    for (const Profile &p : m_userProfiles)
        list.append(QVariantMap{{QStringLiteral("name"), p.name}});
    return list;
}

QVariantList Equalizer::bandLabels() const {
    QVariantList list;
    for (int f : kFrequencies)
        list.append(f >= 1000 ? QStringLiteral("%1K").arg(f / 1000)
                              : QString::number(f));
    return list;
}

void Equalizer::setEnabled(bool on) {
    if (m_enabled == on) return;
    m_enabled = on;
    persist();
    emit enabledChanged();
    // On/off is a deliberate click, not a drag — apply without the debounce so
    // A/B comparison feels immediate.
    applyNow();
}

void Equalizer::setPreamp(double dB) {
    dB = qBound(-GainLimit, dB, GainLimit);
    if (m_autoPreamp || qFuzzyCompare(m_preamp, dB)) return;
    m_preamp = dB;
    persist();
    emit preampChanged();
    scheduleApply();
}

void Equalizer::setAutoPreamp(bool on) {
    if (m_autoPreamp == on) return;
    m_autoPreamp = on;
    persist();
    emit autoPreampChanged();
    emit preampChanged();
    scheduleApply();
}

void Equalizer::setGain(int band, double dB) {
    if (band < 0 || band >= BandCount) return;
    dB = qBound(-GainLimit, dB, GainLimit);
    if (qFuzzyCompare(m_gains[band] + 1.0, dB + 1.0)) return;
    m_gains[band] = dB;
    setActiveProfile(QString());   // hand-edited → "Custom"
    persist();
    emit gainsChanged();
    if (m_autoPreamp) emit preampChanged();
    scheduleApply();
}

void Equalizer::setActiveProfile(const QString &name) {
    if (m_activeProfile == name) return;
    m_activeProfile = name;
    emit activeProfileChanged();
}

int Equalizer::userProfileIndex(const QString &name) const {
    for (int i = 0; i < m_userProfiles.size(); ++i)
        if (m_userProfiles[i].name == name) return i;
    return -1;
}

void Equalizer::applyProfile(const QString &name) {
    const int idx = userProfileIndex(name);
    if (idx < 0) return;

    m_gains = m_userProfiles[idx].gains;
    setActiveProfile(name);
    persist();
    emit gainsChanged();
    emit preampChanged();
    // A profile flip is the headline interaction — no debounce.
    applyNow();
}

void Equalizer::saveProfile(const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;

    const int idx = userProfileIndex(trimmed);
    if (idx >= 0) m_userProfiles[idx].gains = m_gains;
    else          m_userProfiles.append({trimmed, m_gains});

    persistProfiles();
    setActiveProfile(trimmed);
    persist();
    emit profilesChanged();
}

void Equalizer::deleteProfile(const QString &name) {
    const int idx = userProfileIndex(name);
    if (idx < 0) return;
    m_userProfiles.removeAt(idx);
    persistProfiles();
    // The gains stay as they are — only the label reverts to "Custom".
    if (m_activeProfile == name) {
        setActiveProfile(QString());
        persist();
    }
    emit profilesChanged();
}

void Equalizer::reset() {
    m_gains.fill(0.0);
    setActiveProfile(QString());
    persist();
    emit gainsChanged();
    emit preampChanged();
    applyNow();
}

QString Equalizer::filterString() const {
    if (!m_enabled) return QString();

    QStringList parts;
    const double pre = effectivePreamp();
    if (!qFuzzyIsNull(pre))
        parts << QStringLiteral("volume=%1dB").arg(pre, 0, 'f', 1);
    for (int i = 0; i < BandCount; ++i) {
        if (qFuzzyIsNull(m_gains[i])) continue;   // flat band = no biquad
        // One-octave peaking filter per band (t=o:w=1), matching the octave
        // spacing of the centers.
        parts << QStringLiteral("equalizer=f=%1:t=o:w=1:g=%2")
                     .arg(kFrequencies[i])
                     .arg(m_gains[i], 0, 'f', 1);
    }
    if (parts.isEmpty()) return QString();   // flat EQ = bit-perfect passthrough

    // Labeled @eq so nothing else in a future af chain gets clobbered.
    return QStringLiteral("@eq:lavfi=[%1]").arg(parts.join(QLatin1Char(',')));
}

void Equalizer::scheduleApply() {
    m_applyTimer->start();
}

void Equalizer::applyNow() {
    m_applyTimer->stop();
    if (m_playerCtl) m_playerCtl->setAudioFilter(filterString());
}
