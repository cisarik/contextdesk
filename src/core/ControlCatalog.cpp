#include "core/ControlCatalog.h"

#include <QSet>

namespace contextdeck {
namespace {

const QVector<ControlInfo> kCatalog = {
    {ControlId::F1, "F1", false},
    {ControlId::F2, "F2", false},
    {ControlId::F3, "F3", false},
    {ControlId::F4, "F4", false},
    {ControlId::F5, "F5", false},
    {ControlId::F6, "F6", false},
    {ControlId::F7, "F7", false},
    {ControlId::F8, "F8", false},
    {ControlId::F9, "F9", false},
    {ControlId::F10, "F10", false},
    {ControlId::F11, "F11", false},
    {ControlId::F12, "F12", false},
    {ControlId::Previous, "Previous", false},
    {ControlId::PlayPause, "PlayPause", false},
    {ControlId::Next, "Next", false},
    {ControlId::Mute, "Mute", false},
    {ControlId::VolumeDown, "VolumeDown", false},
    {ControlId::VolumeUp, "VolumeUp", false},
    {ControlId::GameMode, "GameMode", true},
    {ControlId::Backlight, "Backlight", true},
};

QSet<QString> buildChordKeyTable()
{
    QSet<QString> keys;
    for (char c = 'a'; c <= 'z'; ++c) {
        keys.insert(QString(QChar::fromLatin1(c)));
    }
    for (char c = '0'; c <= '9'; ++c) {
        keys.insert(QString(QChar::fromLatin1(c)));
    }
    for (int i = 1; i <= 24; ++i) {
        keys.insert(QStringLiteral("f%1").arg(i));
    }
    const QStringList named{
        QStringLiteral("esc"),
        QStringLiteral("escape"),
        QStringLiteral("tab"),
        QStringLiteral("space"),
        QStringLiteral("enter"),
        QStringLiteral("backspace"),
        QStringLiteral("insert"),
        QStringLiteral("delete"),
        QStringLiteral("home"),
        QStringLiteral("end"),
        QStringLiteral("pageup"),
        QStringLiteral("pagedown"),
        QStringLiteral("up"),
        QStringLiteral("down"),
        QStringLiteral("left"),
        QStringLiteral("right"),
        QStringLiteral("minus"),
        QStringLiteral("equal"),
        QStringLiteral("leftbrace"),
        QStringLiteral("rightbrace"),
        QStringLiteral("backslash"),
        QStringLiteral("semicolon"),
        QStringLiteral("apostrophe"),
        QStringLiteral("grave"),
        QStringLiteral("comma"),
        QStringLiteral("dot"),
        QStringLiteral("slash"),
        QStringLiteral("capslock"),
        QStringLiteral("numlock"),
        QStringLiteral("scrolllock"),
        QStringLiteral("compose"),
        QStringLiteral("menu"),
    };
    for (const QString &name : named) {
        keys.insert(name);
    }
    return keys;
}

const QSet<QString> &chordKeyTable()
{
    static const QSet<QString> keys = buildChordKeyTable();
    return keys;
}

} // namespace

QVector<ControlInfo> controlCatalog()
{
    return kCatalog;
}

std::optional<ControlId> controlFromJsonName(QStringView name)
{
    for (const ControlInfo &info : kCatalog) {
        if (name == QLatin1String(info.jsonName)) {
            return info.id;
        }
    }
    return std::nullopt;
}

QString controlJsonName(ControlId id)
{
    for (const ControlInfo &info : kCatalog) {
        if (info.id == id) {
            return QString::fromLatin1(info.jsonName);
        }
    }
    return {};
}

bool controlIsConditional(ControlId id)
{
    for (const ControlInfo &info : kCatalog) {
        if (info.id == id) {
            return info.conditionalOnHardwareEvidence;
        }
    }
    return false;
}

bool isAllowedChordKey(const QString &key)
{
    return chordKeyTable().contains(key);
}

bool chordKeyLooksLikeShellOrPath(const QString &key)
{
    if (key.contains(QLatin1Char('/')) || key.contains(QLatin1Char('\\')) || key.contains(QLatin1Char('~'))) {
        return true;
    }
    if (key.contains(QLatin1Char(' ')) || key.contains(QLatin1Char('\t'))) {
        return true;
    }
    static const QString meta = QStringLiteral("$;`|&<>()*?[]{}!");
    for (QChar ch : meta) {
        if (key.contains(ch)) {
            return true;
        }
    }
    if (key.contains(QLatin1Char('.')) && (key.endsWith(QLatin1String("sh")) || key.contains(QLatin1Char('/')))) {
        return true;
    }
    return false;
}

std::optional<Modifier> modifierFromJsonName(QStringView name)
{
    if (name == QLatin1String("ctrl")) {
        return Modifier::Ctrl;
    }
    if (name == QLatin1String("shift")) {
        return Modifier::Shift;
    }
    if (name == QLatin1String("alt")) {
        return Modifier::Alt;
    }
    if (name == QLatin1String("super")) {
        return Modifier::Super;
    }
    return std::nullopt;
}

QString modifierJsonName(Modifier modifier)
{
    switch (modifier) {
    case Modifier::Ctrl:
        return QStringLiteral("ctrl");
    case Modifier::Shift:
        return QStringLiteral("shift");
    case Modifier::Alt:
        return QStringLiteral("alt");
    case Modifier::Super:
        return QStringLiteral("super");
    }
    return {};
}

std::optional<ActionType> actionFromJsonName(QStringView name)
{
    if (name == QLatin1String("inherit_global")) {
        return ActionType::InheritGlobal;
    }
    if (name == QLatin1String("pass_through")) {
        return ActionType::PassThrough;
    }
    if (name == QLatin1String("disabled")) {
        return ActionType::Disabled;
    }
    if (name == QLatin1String("emit_shortcut")) {
        return ActionType::EmitShortcut;
    }
    if (name == QLatin1String("approved_system_action")) {
        return ActionType::ApprovedSystemAction;
    }
    return std::nullopt;
}

QString actionJsonName(ActionType action)
{
    switch (action) {
    case ActionType::InheritGlobal:
        return QStringLiteral("inherit_global");
    case ActionType::PassThrough:
        return QStringLiteral("pass_through");
    case ActionType::Disabled:
        return QStringLiteral("disabled");
    case ActionType::EmitShortcut:
        return QStringLiteral("emit_shortcut");
    case ActionType::ApprovedSystemAction:
        return QStringLiteral("approved_system_action");
    }
    return {};
}

std::optional<SystemActionId> systemActionFromJsonName(QStringView name)
{
    if (name == QLatin1String("suspend")) {
        return SystemActionId::Suspend;
    }
    if (name == QLatin1String("displays_off")) {
        return SystemActionId::DisplaysOff;
    }
    return std::nullopt;
}

QString systemActionJsonName(SystemActionId id)
{
    switch (id) {
    case SystemActionId::Suspend:
        return QStringLiteral("suspend");
    case SystemActionId::DisplaysOff:
        return QStringLiteral("displays_off");
    }
    return {};
}

std::optional<LightingMode> lightingModeFromJsonName(QStringView name)
{
    if (name == QLatin1String("automatic")) {
        return LightingMode::Automatic;
    }
    if (name == QLatin1String("temporary_color")) {
        return LightingMode::TemporaryColor;
    }
    if (name == QLatin1String("lights_off")) {
        return LightingMode::LightsOff;
    }
    return std::nullopt;
}

QString lightingModeJsonName(LightingMode mode)
{
    switch (mode) {
    case LightingMode::Automatic:
        return QStringLiteral("automatic");
    case LightingMode::TemporaryColor:
        return QStringLiteral("temporary_color");
    case LightingMode::LightsOff:
        return QStringLiteral("lights_off");
    }
    return {};
}

} // namespace contextdeck
