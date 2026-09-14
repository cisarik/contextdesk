#pragma once

#include "core/Types.h"

#include <QObject>
#include <QString>
#include <QStringList>

namespace contextdeck {

class ContextReceiver;
class OpenRgbClient;
class ProfileStore;

enum class SessionLightingMode {
    Automatic,
    TemporaryColor,
    LightsOff,
    DeviceDefault,
};

// Owns the session lighting mode, the lighting/profile edits of the loaded
// profile document, and persistence of that document.
class ProfileDocumentEditor : public QObject
{
    Q_OBJECT

public:
    ProfileDocumentEditor(ProfileStore &store, ProfileDocument &document, ContextReceiver *context,
                          OpenRgbClient *rgb, SessionLightingMode &sessionLighting, Rgb &temporaryColor,
                          QObject *parent = nullptr);

    [[nodiscard]] QString saveStatus() const { return m_saveStatus; }

    bool save();
    void setGlobalColor(const QString &hex);
    void setApplicationColor(const QString &id, const QString &hex);
    void addProfileFromInventory(int index);
    void removeProfile(const QString &id);
    void setAutomatic(bool enabled);
    void lightsOff();
    void restoreAutomatic();
    void restoreDeviceDefault();
    void setTemporaryColor(const QString &hex);
    void setGlobalLightingMode(const QString &modeName);
    void setGlobalZoneColor(int index, const QString &hex);
    void applyGlobalGradient(const QString &startHex, const QString &endHex);
    void setGlobalZoneRole(int index, const QString &roleName);
    void useDefaultWorkspaceLayout();
    void useStaticZoneLayout();
    void setGlobalSpeed(int percent);
    void setGlobalBreathingColor(const QString &hex);
    void setApplicationLightingMode(const QString &id, const QString &modeName);
    void setApplicationZoneColor(const QString &id, int index, const QString &hex);
    void applyApplicationGradient(const QString &id, const QString &startHex, const QString &endHex);
    void setApplicationSpeed(const QString &id, int percent);
    void setApplicationBreathingColor(const QString &id, const QString &hex);
    void assignEmitShortcut(const QString &controlName, const QString &key, const QStringList &modifiers,
                            bool applicationLevel, const QString &applicationId);
    void expireTemporaryColor();

signals:
    void documentChanged();
    void lightingModeChanged();
    void applyLightingRequested();
    void resolvedProfileInvalidated();

private:
    ProfileStore &m_store;
    ProfileDocument &m_document;
    ContextReceiver *m_context = nullptr;
    OpenRgbClient *m_rgb = nullptr;
    SessionLightingMode &m_sessionLighting;
    Rgb &m_temporaryColor;
    QString m_saveStatus;
};

} // namespace contextdeck
