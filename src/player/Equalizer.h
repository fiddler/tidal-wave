#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

class Player;
class QTimer;

// 10-band graphic equalizer with saveable profiles.
//
// The DSP lives inside libmpv (FFmpeg's `equalizer` biquads plus a `volume`
// preamp) — this class only turns the band gains into an mpv "af" filter
// string and hands it to Player. mpv swaps the chain mid-playback without a
// dropout, so profile flips are instant, and the property persists across
// track changes on its own.
//
// Scope: local mpv output only. Chromecast streams the file bytes straight to
// the device, so the EQ never touches that path (the UI says so).
class Equalizer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool         enabled       READ enabled    WRITE setEnabled    NOTIFY enabledChanged)
    Q_PROPERTY(QVariantList gains         READ gains                          NOTIFY gainsChanged)
    Q_PROPERTY(double       preamp        READ preamp     WRITE setPreamp     NOTIFY preampChanged)
    Q_PROPERTY(bool         autoPreamp    READ autoPreamp WRITE setAutoPreamp NOTIFY autoPreampChanged)
    // User-saved profiles: [{name}]. No built-in presets — the dropdown is
    // exactly what the user chose to save.
    Q_PROPERTY(QVariantList profiles      READ profiles                       NOTIFY profilesChanged)
    // Name of the profile the current gains came from; "" once a band is
    // hand-edited (the UI shows "Custom").
    Q_PROPERTY(QString      activeProfile READ activeProfile                  NOTIFY activeProfileChanged)
    Q_PROPERTY(QVariantList bandLabels    READ bandLabels CONSTANT)
    Q_PROPERTY(double       gainLimit     READ gainLimit  CONSTANT)

public:
    static constexpr int    BandCount = 10;
    static constexpr double GainLimit = 24.0;   // ± dB per band and preamp, like eqMac

    double gainLimit() const { return GainLimit; }

    explicit Equalizer(Player *player, QObject *parent = nullptr);
    ~Equalizer() override;

    bool         enabled()       const { return m_enabled; }
    QVariantList gains()         const;
    // With auto preamp on this reports the computed headroom value, so the
    // (disabled) slider always shows what is actually applied.
    double       preamp()        const;
    bool         autoPreamp()    const { return m_autoPreamp; }
    QVariantList profiles()      const;
    QString      activeProfile() const { return m_activeProfile; }
    QVariantList bandLabels()    const;

    void setEnabled(bool on);
    void setPreamp(double dB);
    void setAutoPreamp(bool on);

    Q_INVOKABLE void setGain(int band, double dB);
    Q_INVOKABLE void applyProfile(const QString &name);
    Q_INVOKABLE void saveProfile(const QString &name);
    Q_INVOKABLE void deleteProfile(const QString &name);
    Q_INVOKABLE void reset();

signals:
    void enabledChanged();
    void gainsChanged();
    void preampChanged();
    void autoPreampChanged();
    void profilesChanged();
    void activeProfileChanged();

private:
    struct Profile {
        QString         name;
        QVector<double> gains;
    };

    void    load();
    void    persist() const;
    // Settings writes are debounced like the mpv pushes: a fader drag emits
    // ~2 steps per pixel and each QSettings sync is a plist write.
    void    schedulePersist();
    void    persistProfiles() const;
    void    setActiveProfile(const QString &name);
    // Preamp that is actually applied: the manual value, or with auto on the
    // negative of the cascade's estimated peak boost (neighbouring octave
    // bands overlap, so the estimate sums each band with a sampled fraction
    // of its neighbours rather than taking the largest slider alone).
    double  effectivePreamp() const;
    QString filterString() const;
    // Debounced: slider drags fire per pixel, mpv only needs the settled value.
    void    scheduleApply();
    void    applyNow();
    int     userProfileIndex(const QString &name) const;

    Player          *m_playerCtl = nullptr;
    QTimer          *m_applyTimer = nullptr;
    QTimer          *m_persistTimer = nullptr;
    QVector<double>  m_gains;
    QVector<Profile> m_userProfiles;
    QString          m_activeProfile;
    double           m_preamp     = 0.0;
    bool             m_autoPreamp = false;
    bool             m_enabled    = false;
};
