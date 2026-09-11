#include "app/AppController.h"

#include "app/BrokerIpcClient.h"
#include "context/DBusNames.h"
#include "core/ControlCatalog.h"
#include "core/Resolver.h"
#include "core/ZoneMap.h"

#include <QLoggingCategory>
#include <QStringList>

#include <algorithm>

namespace contextdeck {

Q_LOGGING_CATEGORY(lcUi, "contextdeck.ui")

namespace {

Lighting defaultLighting()
{
    return untouchedLighting();
}

Rgb displayColor(const Lighting &lighting)
{
    if (lighting.baseColor) {
        return *lighting.baseColor;
    }
    if (lighting.zones) {
        return (*lighting.zones)[0].color;
    }
    return Rgb{};
}

QString hexOf(const Rgb &color)
{
    return QStringLiteral("#%1%2%3")
        .arg(color.r, 2, 16, QLatin1Char('0'))
        .arg(color.g, 2, 16, QLatin1Char('0'))
        .arg(color.b, 2, 16, QLatin1Char('0'));
}

QStringList zoneHexList(const Lighting &lighting)
{
    QStringList list;
    const std::array<Rgb, kZoneCount> colors = zoneColorsFromPreset(lighting);
    for (const Rgb &color : colors) {
        list.push_back(hexOf(color));
    }
    return list;
}

void ensureDirectColors(Lighting &lighting, const Rgb &fallback)
{
    if (!lighting.baseColor && !lighting.zones) {
        lighting.baseColor = fallback;
    }
}

bool applyMode(Lighting &lighting, LightingMode mode, const Rgb &fallback)
{
    lighting.mode = mode;
    if (mode == LightingMode::Direct) {
        ensureDirectColors(lighting, fallback);
    }
    if (mode == LightingMode::Breathing && !lighting.baseColor) {
        lighting.baseColor = fallback;
    }
    return true;
}

bool applyZoneColor(Lighting &lighting, int index, const Rgb &color, const Rgb &fallback)
{
    if (index < 0 || index >= kZoneCount) {
        return false;
    }
    std::array<Rgb, kZoneCount> colors = zoneColorsFromPreset(lighting);
    if (!lighting.zones && !lighting.baseColor) {
        colors.fill(fallback);
    }
    colors[static_cast<size_t>(index)] = color;
    lighting.zones = zoneValuesFromColors(colors);
    lighting.baseColor = color;
    lighting.mode = LightingMode::Direct;
    return true;
}

bool applyGradient(Lighting &lighting, const Rgb &start, const Rgb &end)
{
    lighting.zones = zoneValuesFromColors(gradientColors(start, end));
    lighting.baseColor = start;
    lighting.mode = LightingMode::Direct;
    return true;
}

} // namespace

AppController::AppController(ContextReceiver *context, OpenRgbClient *rgb, PowerActions *power, QObject *parent)
    : QObject(parent)
    , m_context(context)
    , m_rgb(rgb)
    , m_power(power)
{
    m_document.globalLighting = defaultLighting();
    connect(m_context, &ContextReceiver::currentIdentityChanged, this, &AppController::onIdentityChanged);
    connect(m_context, &ContextReceiver::bridgeLost, this, &AppController::onIdentityChanged);
    connect(m_context, &ContextReceiver::bridgeConnectedChanged, this, &AppController::contextChanged);
    connect(m_context, &ContextReceiver::inventoryChanged, this, &AppController::onInventoryChanged);
    connect(m_context, &ContextReceiver::degradedChanged, this, &AppController::contextChanged);
    connect(m_rgb, &OpenRgbClient::connectionStateChanged, this, &AppController::diagnosticsChanged);
    connect(m_rgb, &OpenRgbClient::lastErrorChanged, this, &AppController::diagnosticsChanged);
    connect(m_rgb, &OpenRgbClient::lightingEnabledChanged, this, &AppController::diagnosticsChanged);
    connect(this, &AppController::contextChanged, this, &AppController::presentationChanged);
    connect(this, &AppController::lightingModeChanged, this, &AppController::presentationChanged);
    connect(this, &AppController::diagnosticsChanged, this, &AppController::presentationChanged);
    connect(this, &AppController::documentChanged, this, &AppController::presentationChanged);
}

void AppController::setBrokerIpc(BrokerIpcClient *client)
{
    m_brokerIpc = client;
    if (m_brokerIpc == nullptr) {
        return;
    }
    connect(m_brokerIpc, &BrokerIpcClient::stateChanged, this, &AppController::diagnosticsChanged);
    connect(m_brokerIpc, &BrokerIpcClient::stateChanged, this, &AppController::presentationChanged);
}

void AppController::load()
{
    const LoadOutcome loaded = m_store.load();
    if (loaded.ok && !loaded.missing) {
        m_document = loaded.document;
        qCInfo(lcUi) << "loaded profile document";
    } else if (loaded.missing) {
        m_document = ProfileDocument{};
        m_document.globalLighting = defaultLighting();
        qCInfo(lcUi) << "cold start: no profile document, pass-through, writing nothing";
    } else {
        qCWarning(lcUi) << "profile load refused:" << loaded.error.reason << loaded.error.jsonPath
                        << "preserved=" << loaded.error.preserved;
        m_document = ProfileDocument{};
        m_document.globalLighting = defaultLighting();
    }
    if (m_document.preferences.automaticEnabled) {
        m_sessionLighting = SessionLightingMode::Automatic;
    }
    rememberExternalContext();
    refreshResolvedProfile();
    applyLighting();
    emit documentChanged();
    emit contextChanged();
}

QString AppController::currentApplication() const
{
    const ApplicationIdentity identity = m_context->identity();
    if (!identity.isIdentified()) {
        return QStringLiteral("(unidentified)");
    }
    if (!identity.desktopFileName.isEmpty()) {
        return identity.desktopFileName;
    }
    if (!identity.resourceClass.isEmpty()) {
        return identity.resourceClass;
    }
    return identity.resourceName;
}

QString AppController::currentProfile() const
{
    if (m_resolvedProfileId.isEmpty()) {
        return QStringLiteral("global");
    }
    return m_resolvedProfileId;
}

QString AppController::lightingMode() const
{
    if (m_sessionLighting == SessionLightingMode::TemporaryColor) {
        return QStringLiteral("temporary:") + lightingModeJsonName(effectiveLighting().mode);
    }
    return lightingModeJsonName(effectiveLighting().mode);
}

QString AppController::lightingLabel() const
{
    if (m_sessionLighting == SessionLightingMode::TemporaryColor) {
        return QStringLiteral("temporary override — %1").arg(lightingModeJsonName(effectiveLighting().mode));
    }
    if (m_sessionLighting == SessionLightingMode::LightsOff) {
        return QStringLiteral("off");
    }
    if (m_sessionLighting == SessionLightingMode::DeviceDefault
        || effectiveLighting().mode == LightingMode::Untouched) {
        return QStringLiteral("untouched — device default");
    }
    return lightingModeJsonName(effectiveLighting().mode);
}

bool AppController::temporaryOverrideActive() const
{
    return m_sessionLighting == SessionLightingMode::TemporaryColor;
}

QString AppController::sessionLighting() const
{
    switch (m_sessionLighting) {
    case SessionLightingMode::Automatic:
        return QStringLiteral("automatic");
    case SessionLightingMode::TemporaryColor:
        return QStringLiteral("temporary");
    case SessionLightingMode::LightsOff:
        return QStringLiteral("off");
    case SessionLightingMode::DeviceDefault:
        return QStringLiteral("device_default");
    }
    return QStringLiteral("automatic");
}

QString AppController::lightingConnection() const
{
    switch (m_rgb->connectionState()) {
    case LightingConnectionState::Disconnected:
        return QStringLiteral("disconnected");
    case LightingConnectionState::Connecting:
        return QStringLiteral("connecting");
    case LightingConnectionState::Negotiating:
        return QStringLiteral("negotiating");
    case LightingConnectionState::Ready:
        return QStringLiteral("ready");
    case LightingConnectionState::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

QString AppController::lastError() const
{
    if (!m_rgb->lastError().isEmpty()) {
        return m_rgb->lastError();
    }
    if (!m_context->lastError().isEmpty()) {
        return m_context->lastError();
    }
    return m_power->lastError();
}

bool AppController::bridgeConnected() const
{
    return m_context->bridgeConnected();
}

bool AppController::degraded() const
{
    return m_context->isDegraded();
}

QString AppController::globalColor() const
{
    return toHex(displayColor(m_document.globalLighting));
}

QString AppController::globalMode() const
{
    return lightingModeJsonName(m_document.globalLighting.mode);
}

QStringList AppController::globalZones() const
{
    return zoneHexList(m_document.globalLighting);
}

QStringList AppController::zoneNames() const
{
    QStringList names;
    for (const char *name : kZoneNames) {
        names.push_back(QString::fromUtf8(name));
    }
    return names;
}

QStringList AppController::lightingPresets() const
{
    return {
        QStringLiteral("untouched"),
        QStringLiteral("wave"),
        QStringLiteral("cycle"),
        QStringLiteral("breathing"),
        QStringLiteral("off"),
        QStringLiteral("direct"),
    };
}

QStringList AppController::lightingPresetLabels() const
{
    return {
        QStringLiteral("Predvolené firmware (Wave)"),
        QStringLiteral("Wave"),
        QStringLiteral("Cycle"),
        QStringLiteral("Breathing"),
        QStringLiteral("Vypnuté"),
        QStringLiteral("Vlastné farby"),
    };
}

int AppController::globalSpeedPercent() const
{
    return speedToPercent(m_document.globalLighting);
}

QString AppController::globalBreathingColor() const
{
    return toHex(m_document.globalLighting.baseColor.value_or(kDefaultEffectColor));
}

QString AppController::statusSummary() const
{
    return QStringLiteral("%1 · kontext: %2 · svetlá: %3")
        .arg(openRgbPhrase(), contextDisplayName(), lightsPhrase());
}

bool AppController::isSelfWindow() const
{
    return isOwnSurface(m_context->identity());
}

QString AppController::lastExternalApplication() const
{
    return m_lastExternalApplication;
}

QString AppController::contextDisplayName() const
{
    if (isSelfWindow()) {
        if (!m_lastExternalApplication.isEmpty()) {
            return QStringLiteral("Posledná aplikácia: %1").arg(m_lastExternalApplication);
        }
        return QStringLiteral("ContextDeck (toto okno)");
    }
    const ApplicationIdentity identity = m_context->identity();
    if (!identity.isIdentified()) {
        return QStringLiteral("neidentifikovaný");
    }
    return friendlyApplicationName(identity);
}

bool AppController::hasSavedProfiles() const
{
    if (!m_document.applications.isEmpty()) {
        return true;
    }
    return m_document.globalLighting.mode != LightingMode::Untouched;
}

QString AppController::heroKind() const
{
    const Lighting lighting = effectiveLighting();
    if (lighting.mode == LightingMode::Off) {
        return QStringLiteral("off");
    }
    if (lighting.mode == LightingMode::Untouched) {
        return QStringLiteral("untouched");
    }
    if (lighting.mode == LightingMode::Direct) {
        return QStringLiteral("direct");
    }
    return QStringLiteral("effect");
}

QString AppController::heroBadge() const
{
    const Lighting lighting = effectiveLighting();
    if (m_sessionLighting == SessionLightingMode::TemporaryColor) {
        return QStringLiteral("Temporary override");
    }
    if (lighting.mode == LightingMode::Untouched) {
        QString restore = lightingModeJsonName(m_rgb->recordedRestoreMode());
        if (restore.isEmpty()) {
            restore = QStringLiteral("wave");
        }
        restore[0] = restore[0].toUpper();
        return QStringLiteral("Device default (%1)").arg(restore);
    }
    if (lighting.mode == LightingMode::Wave) {
        return QStringLiteral("Wave");
    }
    if (lighting.mode == LightingMode::Cycle) {
        return QStringLiteral("Cycle");
    }
    if (lighting.mode == LightingMode::Breathing) {
        return QStringLiteral("Breathing");
    }
    if (lighting.mode == LightingMode::Off) {
        return QStringLiteral("Off");
    }
    if (lighting.mode == LightingMode::Direct) {
        return QStringLiteral("Direct");
    }
    return lightingModeJsonName(lighting.mode);
}

QStringList AppController::heroZones() const
{
    return zoneHexList(effectiveLighting());
}

QVariantList AppController::inventory() const
{
    QVariantList list;
    for (const InventoryEntry &entry : m_context->inventory()) {
        QVariantMap map;
        map.insert(QStringLiteral("desktopFileName"), entry.desktopFileName);
        map.insert(QStringLiteral("resourceClass"), entry.resourceClass);
        map.insert(QStringLiteral("resourceName"), entry.resourceName);
        const QString label = !entry.desktopFileName.isEmpty() ? entry.desktopFileName : entry.resourceClass;
        map.insert(QStringLiteral("label"), label);
        list.push_back(map);
    }
    return list;
}

QVariantList AppController::profiles() const
{
    QVariantList list;
    for (const ApplicationProfile &profile : m_document.applications) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), profile.id);
        map.insert(QStringLiteral("displayName"), profile.displayName);
        map.insert(QStringLiteral("color"),
                   toHex(displayColor(profile.lighting.value_or(m_document.globalLighting))));
        const Lighting lighting = profile.lighting.value_or(m_document.globalLighting);
        map.insert(QStringLiteral("mode"), lightingModeJsonName(lighting.mode));
        map.insert(QStringLiteral("zones"), zoneHexList(lighting));
        map.insert(QStringLiteral("speedPercent"), speedToPercent(lighting));
        map.insert(QStringLiteral("breathingColor"), toHex(lighting.baseColor.value_or(kDefaultEffectColor)));
        list.push_back(map);
    }
    return list;
}

QVariantList AppController::controls() const
{
    QVariantList list;
    const ApplicationIdentity identity = m_context->identity();
    for (const ControlInfo &info : controlCatalog()) {
        const Assignment assignment = resolveAssignment(m_document, identity, info.id);
        QVariantMap map;
        map.insert(QStringLiteral("name"), QString::fromLatin1(info.jsonName));
        map.insert(QStringLiteral("conditional"), info.conditionalOnHardwareEvidence);
        map.insert(QStringLiteral("action"), actionJsonName(assignment.action));
        const ZoneMapEntry *zone = zoneMapEntry(info.id);
        if (zone != nullptr) {
            map.insert(QStringLiteral("zoneName"), QString::fromUtf8(zone->zoneName));
            map.insert(QStringLiteral("zoneVerified"), zone->verified);
            map.insert(QStringLiteral("zonePreview"),
                       zone->verified ? QStringLiteral("verified zone")
                                      : QStringLiteral("unverified preview — no accent writes"));
        }
        map.insert(QStringLiteral("note"),
                   info.conditionalOnHardwareEvidence
                       ? QStringLiteral("Conditional on hardware evidence (G1); not bound in M1.")
                       : QStringLiteral("emit_shortcut is stored but not active until the input broker exists."));
        list.push_back(map);
    }
    return list;
}

QVariantMap AppController::diagnostics() const
{
    QVariantMap map;
    map.insert(QStringLiteral("bridgeConnected"), m_context->bridgeConnected());
    map.insert(QStringLiteral("degraded"), m_context->isDegraded());
    map.insert(QStringLiteral("policyRevision"), m_context->policyRevision());
    map.insert(QStringLiteral("lightingConnection"), lightingConnection());
    map.insert(QStringLiteral("lightingLabel"), lightingLabel());
    map.insert(QStringLiteral("sessionLighting"), sessionLighting());
    map.insert(QStringLiteral("lightingEnabled"), m_rgb->lightingEnabled());
    map.insert(QStringLiteral("hasG213"), m_rgb->hasG213());
    map.insert(QStringLiteral("identityUpdates"), QVariant::fromValue(m_identityUpdates));
    map.insert(QStringLiteral("lightingUpdates"), QVariant::fromValue(m_lightingUpdates));
    map.insert(QStringLiteral("lastError"), lastError());
    map.insert(QStringLiteral("inventoryCount"), m_context->inventory().size());
    map.insert(QStringLiteral("dbusService"), QString(kServiceName));
    map.insert(QStringLiteral("dbusObjectPath"), QString(kContextObjectPath));
    map.insert(QStringLiteral("dbusInterface"), QString(kContextInterface));
    map.insert(QStringLiteral("bridgeId"), m_context->bridgeId());
    map.insert(QStringLiteral("currentIdentity"), m_context->currentIdentity());
    map.insert(QStringLiteral("socketState"), m_rgb->socketStateText());
    map.insert(QStringLiteral("sdkEndpoint"), m_rgb->sdkEndpoint());
    map.insert(QStringLiteral("brokerIpcState"), brokerIpcState());
    map.insert(QStringLiteral("isSelfWindow"), isSelfWindow());
    map.insert(QStringLiteral("lastExternalApplication"), m_lastExternalApplication);
    return map;
}

bool AppController::save()
{
    const SaveOutcome outcome = m_store.save(m_document);
    if (!outcome.ok) {
        m_saveStatus = outcome.error.reason;
        qCWarning(lcUi) << "save refused:" << outcome.error.reason << outcome.error.jsonPath
                        << "preserved=" << outcome.error.preserved;
        return false;
    }
    m_saveStatus = QStringLiteral("saved");
    emit documentChanged();
    applyLighting();
    return true;
}

void AppController::setGlobalColor(const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    m_document.globalLighting.baseColor = *color;
    m_document.globalLighting.mode = LightingMode::Direct;
    m_document.globalLighting.zones.reset();
    m_sessionLighting = SessionLightingMode::Automatic;
    emit documentChanged();
    applyLighting();
}

void AppController::setApplicationColor(const QString &id, const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = profile.lighting.value_or(m_document.globalLighting);
            lighting.baseColor = *color;
            lighting.mode = LightingMode::Direct;
            lighting.zones.reset();
            profile.lighting = lighting;
            emit documentChanged();
            applyLighting();
            return;
        }
    }
}

void AppController::addProfileFromInventory(int index)
{
    const auto entries = m_context->inventory();
    if (index < 0 || index >= entries.size()) {
        return;
    }
    const InventoryEntry &entry = entries.at(index);
    const QString id = !entry.desktopFileName.isEmpty() ? entry.desktopFileName : entry.resourceClass;
    if (id.isEmpty()) {
        return;
    }
    for (const ApplicationProfile &existing : m_document.applications) {
        if (existing.id == id) {
            return;
        }
    }
    ApplicationProfile profile;
    profile.id = id;
    profile.displayName = id;
    if (!entry.desktopFileName.isEmpty()) {
        profile.match.desktopFileName = entry.desktopFileName;
    } else if (!entry.resourceClass.isEmpty()) {
        profile.match.resourceClass = entry.resourceClass;
    } else {
        profile.match.resourceName = entry.resourceName;
    }
    Lighting lighting = m_document.globalLighting;
    lighting.mode = LightingMode::Direct;
    profile.lighting = lighting;
    m_document.applications.push_back(profile);
    emit documentChanged();
}

void AppController::removeProfile(const QString &id)
{
    for (int i = 0; i < m_document.applications.size(); ++i) {
        if (m_document.applications.at(i).id == id) {
            m_document.applications.removeAt(i);
            emit documentChanged();
            refreshResolvedProfile();
            applyLighting();
            return;
        }
    }
}

void AppController::setAutomatic(bool enabled)
{
    if (enabled) {
        restoreAutomatic();
    } else {
        lightsOff();
    }
}

void AppController::lightsOff()
{
    m_sessionLighting = SessionLightingMode::LightsOff;
    m_document.preferences.automaticEnabled = false;
    emit lightingModeChanged();
    applyLighting();
}

void AppController::restoreAutomatic()
{
    m_sessionLighting = SessionLightingMode::Automatic;
    m_document.preferences.automaticEnabled = true;
    emit lightingModeChanged();
    applyLighting();
}

void AppController::restoreDeviceDefault()
{
    m_sessionLighting = SessionLightingMode::DeviceDefault;
    m_document.preferences.automaticEnabled = true;
    emit lightingModeChanged();
    applyLighting();
}

void AppController::setTemporaryColor(const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    m_temporaryColor = *color;
    m_sessionLighting = SessionLightingMode::TemporaryColor;
    m_document.preferences.automaticEnabled = false;
    emit lightingModeChanged();
    applyLighting();
}

void AppController::setGlobalLightingMode(const QString &modeName)
{
    const auto mode = lightingModeFromJsonName(modeName);
    if (!mode) {
        return;
    }
    applyMode(m_document.globalLighting, *mode, Rgb{0x7c, 0x3a, 0xed});
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    applyLighting();
}

void AppController::setGlobalZoneColor(int index, const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    if (!applyZoneColor(m_document.globalLighting, index, *color, Rgb{0x7c, 0x3a, 0xed})) {
        return;
    }
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    applyLighting();
}

void AppController::applyGlobalGradient(const QString &startHex, const QString &endHex)
{
    const auto start = parseHex(startHex);
    const auto end = parseHex(endHex);
    if (!start || !end) {
        return;
    }
    applyGradient(m_document.globalLighting, *start, *end);
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    applyLighting();
}

void AppController::setGlobalSpeed(int percent)
{
    if (!applySpeedPercent(m_document.globalLighting, percent)) {
        return;
    }
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    applyLighting();
}

void AppController::setGlobalBreathingColor(const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    applyBreathingColor(m_document.globalLighting, *color);
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    applyLighting();
}

void AppController::setApplicationLightingMode(const QString &id, const QString &modeName)
{
    const auto mode = lightingModeFromJsonName(modeName);
    if (!mode) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = profile.lighting.value_or(m_document.globalLighting);
            applyMode(lighting, *mode, Rgb{0x7c, 0x3a, 0xed});
            profile.lighting = lighting;
            emit documentChanged();
            applyLighting();
            return;
        }
    }
}

void AppController::setApplicationZoneColor(const QString &id, int index, const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = profile.lighting.value_or(m_document.globalLighting);
            if (!applyZoneColor(lighting, index, *color, Rgb{0x7c, 0x3a, 0xed})) {
                return;
            }
            profile.lighting = lighting;
            emit documentChanged();
            applyLighting();
            return;
        }
    }
}

void AppController::applyApplicationGradient(const QString &id, const QString &startHex, const QString &endHex)
{
    const auto start = parseHex(startHex);
    const auto end = parseHex(endHex);
    if (!start || !end) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = profile.lighting.value_or(m_document.globalLighting);
            applyGradient(lighting, *start, *end);
            profile.lighting = lighting;
            emit documentChanged();
            applyLighting();
            return;
        }
    }
}

void AppController::setApplicationSpeed(const QString &id, int percent)
{
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = profile.lighting.value_or(m_document.globalLighting);
            if (!applySpeedPercent(lighting, percent)) {
                return;
            }
            profile.lighting = lighting;
            emit documentChanged();
            applyLighting();
            return;
        }
    }
}

void AppController::setApplicationBreathingColor(const QString &id, const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = profile.lighting.value_or(m_document.globalLighting);
            applyBreathingColor(lighting, *color);
            profile.lighting = lighting;
            emit documentChanged();
            applyLighting();
            return;
        }
    }
}

void AppController::displaysOff()
{
    m_power->displaysOff();
    emit diagnosticsChanged();
}

bool AppController::canSuspend() const
{
    return m_power->suspendAllowed();
}

bool AppController::suspend()
{
    const bool ok = m_power->suspend();
    emit diagnosticsChanged();
    return ok;
}

void AppController::assignEmitShortcut(const QString &controlName, const QString &key, const QStringList &modifiers,
                                       bool applicationLevel, const QString &applicationId)
{
    const auto control = controlFromJsonName(controlName);
    if (!control || controlIsConditional(*control)) {
        return;
    }
    if (!isAllowedChordKey(key) || chordKeyLooksLikeShellOrPath(key)) {
        return;
    }
    Assignment assignment;
    assignment.action = ActionType::EmitShortcut;
    Chord chord;
    chord.key = key;
    for (const QString &name : modifiers) {
        const auto modifier = modifierFromJsonName(name);
        if (!modifier) {
            return;
        }
        chord.modifiers.push_back(*modifier);
    }
    assignment.chord = chord;
    if (applicationLevel) {
        for (ApplicationProfile &profile : m_document.applications) {
            if (profile.id == applicationId) {
                profile.keys.insert(*control, assignment);
                emit documentChanged();
                return;
            }
        }
    } else {
        m_document.globalKeys.insert(*control, assignment);
        emit documentChanged();
    }
}

void AppController::onIdentityChanged()
{
    ++m_identityUpdates;
    rememberExternalContext();
    const ApplicationIdentity identity = m_context->identity();
    if (m_sessionLighting == SessionLightingMode::TemporaryColor && !isOwnSurface(identity)
        && identity.isIdentified()) {
        m_sessionLighting = SessionLightingMode::Automatic;
        m_document.preferences.automaticEnabled = true;
        emit lightingModeChanged();
        qCInfo(lcUi) << "temporary_color expired on external identity change";
    }
    refreshResolvedProfile();
    applyLighting();
    emit contextChanged();
    emit diagnosticsChanged();
}

void AppController::onInventoryChanged()
{
    emit inventoryChanged();
    emit diagnosticsChanged();
}

void AppController::rememberExternalContext()
{
    const ApplicationIdentity identity = m_context->identity();
    if (identity.isIdentified() && !isOwnSurface(identity)) {
        m_lastExternalApplication = friendlyApplicationName(identity);
    }
}

void AppController::refreshResolvedProfile()
{
    const ApplicationProfile *profile = matchApplication(m_document, m_context->identity());
    m_resolvedProfileId = profile != nullptr ? profile->id : QString();
}

bool AppController::isOwnSurface(const ApplicationIdentity &identity) const
{
    const auto containsCi = [](const QString &value, const QString &needle) {
        return value.contains(needle, Qt::CaseInsensitive);
    };
    return containsCi(identity.desktopFileName, QStringLiteral("contextdeck"))
        || containsCi(identity.resourceClass, QStringLiteral("contextdeck"))
        || containsCi(identity.resourceName, QStringLiteral("contextdeck"))
        || containsCi(identity.desktopFileName, QStringLiteral("io.github.cisarik.ContextDeck"));
}

QString AppController::friendlyApplicationName(const ApplicationIdentity &identity) const
{
    QString raw;
    if (!identity.desktopFileName.isEmpty()) {
        raw = identity.desktopFileName;
    } else if (!identity.resourceClass.isEmpty()) {
        raw = identity.resourceClass;
    } else {
        raw = identity.resourceName;
    }
    if (raw.endsWith(QLatin1String(".desktop"))) {
        raw.chop(8);
    }
    const int dot = raw.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0 && dot + 1 < raw.size()) {
        raw = raw.sliced(dot + 1);
    }
    if (!raw.isEmpty()) {
        raw[0] = raw[0].toUpper();
    }
    return raw;
}

QString AppController::openRgbPhrase() const
{
    switch (m_rgb->connectionState()) {
    case LightingConnectionState::Ready:
        return QStringLiteral("OpenRGB pripojený");
    case LightingConnectionState::Connecting:
    case LightingConnectionState::Negotiating:
        return QStringLiteral("OpenRGB sa pripája");
    case LightingConnectionState::Failed:
        return QStringLiteral("OpenRGB nedostupný");
    case LightingConnectionState::Disconnected:
        break;
    }
    return QStringLiteral("OpenRGB odpojený");
}

QString AppController::lightsPhrase() const
{
    if (m_sessionLighting == SessionLightingMode::TemporaryColor) {
        return QStringLiteral("dočasné pretíženie");
    }
    const Lighting lighting = effectiveLighting();
    switch (lighting.mode) {
    case LightingMode::Untouched: {
        QString restore = lightingModeJsonName(m_rgb->recordedRestoreMode());
        if (restore.isEmpty()) {
            restore = QStringLiteral("wave");
        }
        restore[0] = restore[0].toUpper();
        return QStringLiteral("firmware %1").arg(restore);
    }
    case LightingMode::Wave:
        return QStringLiteral("Wave");
    case LightingMode::Cycle:
        return QStringLiteral("Cycle");
    case LightingMode::Breathing:
        return QStringLiteral("Breathing");
    case LightingMode::Off:
        return QStringLiteral("vypnuté");
    case LightingMode::Direct:
        return QStringLiteral("vlastné farby");
    }
    return lightingModeJsonName(lighting.mode);
}

Lighting AppController::effectiveLighting() const
{
    if (m_sessionLighting == SessionLightingMode::LightsOff) {
        Lighting lighting;
        lighting.mode = LightingMode::Off;
        return lighting;
    }
    if (m_sessionLighting == SessionLightingMode::DeviceDefault) {
        return untouchedLighting();
    }
    if (m_sessionLighting == SessionLightingMode::TemporaryColor) {
        Lighting lighting;
        lighting.mode = LightingMode::Direct;
        lighting.baseColor = m_temporaryColor;
        return lighting;
    }
    return resolveLighting(m_document, m_context->identity());
}

void AppController::applyLighting()
{
    sendLighting(effectiveLighting());
}

void AppController::sendLighting(const Lighting &lighting)
{
    m_rgb->setDesiredState(toDesiredLighting(lighting));
    ++m_lightingUpdates;
    emit diagnosticsChanged();
}

void AppController::speedBounds(LightingMode mode, quint32 &slowest, quint32 &fastest) const
{
    quint32 speedMin = 0;
    quint32 speedMax = 0;
    if (m_rgb->speedRangeFor(mode, speedMin, speedMax)) {
        slowest = speedMin;
        fastest = speedMax;
        return;
    }
    slowest = 0xC8;
    fastest = 0x0A;
}

quint32 AppController::percentToSpeed(int percent, LightingMode mode) const
{
    quint32 slowest = 0;
    quint32 fastest = 0;
    speedBounds(mode, slowest, fastest);
    const int clamped = std::clamp(percent, 0, 100);
    const qint64 span = static_cast<qint64>(fastest) - static_cast<qint64>(slowest);
    return static_cast<quint32>(static_cast<qint64>(slowest) + span * clamped / 100);
}

int AppController::speedToPercent(const Lighting &lighting) const
{
    if (!lighting.speed.has_value()) {
        return 50;
    }
    quint32 slowest = 0;
    quint32 fastest = 0;
    speedBounds(lighting.mode, slowest, fastest);
    if (slowest == fastest) {
        return 50;
    }
    const qint64 span = static_cast<qint64>(fastest) - static_cast<qint64>(slowest);
    const qint64 delta = static_cast<qint64>(*lighting.speed) - static_cast<qint64>(slowest);
    return std::clamp(static_cast<int>((delta * 100 + span / 2) / span), 0, 100);
}

bool AppController::applySpeedPercent(Lighting &lighting, int percent)
{
    if (percent < 0 || percent > 100) {
        return false;
    }
    LightingMode mode = lighting.mode;
    if (mode != LightingMode::Wave && mode != LightingMode::Cycle && mode != LightingMode::Breathing) {
        mode = LightingMode::Wave;
    }
    lighting.speed = percentToSpeed(percent, mode);
    return true;
}

bool AppController::applyBreathingColor(Lighting &lighting, const Rgb &color)
{
    lighting.baseColor = color;
    if (lighting.mode != LightingMode::Breathing) {
        lighting.mode = LightingMode::Breathing;
    }
    return true;
}

std::optional<Rgb> AppController::parseHex(const QString &hex)
{
    QString text = hex.trimmed();
    if (!text.startsWith(QLatin1Char('#'))) {
        text.prepend(QLatin1Char('#'));
    }
    if (text.size() != 7) {
        return std::nullopt;
    }
    bool ok = false;
    const int rgb = text.sliced(1).toInt(&ok, 16);
    if (!ok) {
        return std::nullopt;
    }
    Rgb color;
    color.r = static_cast<quint8>((rgb >> 16) & 0xff);
    color.g = static_cast<quint8>((rgb >> 8) & 0xff);
    color.b = static_cast<quint8>(rgb & 0xff);
    return color;
}

QString AppController::toHex(const Rgb &color)
{
    return QStringLiteral("#%1%2%3")
        .arg(color.r, 2, 16, QLatin1Char('0'))
        .arg(color.g, 2, 16, QLatin1Char('0'))
        .arg(color.b, 2, 16, QLatin1Char('0'));
}

QStringList AppController::previewGradient(const QString &startHex, const QString &endHex) const
{
    const auto start = parseHex(startHex);
    const auto end = parseHex(endHex);
    if (!start || !end) {
        return {};
    }
    QStringList list;
    for (const Rgb &color : gradientColors(*start, *end)) {
        list.push_back(toHex(color));
    }
    return list;
}

bool AppController::isValidHex(const QString &hex) const
{
    return parseHex(hex).has_value();
}

void AppController::armPassThrough()
{
    if (m_brokerIpc != nullptr) {
        m_brokerIpc->arm();
    }
}

void AppController::disarmPassThrough()
{
    if (m_brokerIpc != nullptr) {
        m_brokerIpc->disarm();
    }
}

void AppController::releaseBrokerLease()
{
    if (m_brokerIpc != nullptr) {
        m_brokerIpc->releaseLease();
    }
}

QString AppController::brokerIpcState() const
{
    if (m_brokerIpc == nullptr) {
        return QStringLiteral("disconnected");
    }
    return m_brokerIpc->stateText();
}

} // namespace contextdeck
