#include "core/persistence/KeysJson.h"

#include "core/ControlCatalog.h"
#include "core/persistence/JsonCommon.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

namespace contextdeck::persistence {
namespace {

std::optional<Chord> parseChord(const QJsonObject &object, const QString &path, PersistenceError &error)
{
    static const QStringList allowed{QStringLiteral("key"), QStringLiteral("modifiers")};
    if (!checkObjectKeys(object, allowed, path, error)) {
        return std::nullopt;
    }
    if (!object.contains(QStringLiteral("key")) || !object.value(QStringLiteral("key")).isString()) {
        error = makeError(QStringLiteral("chord.key must be a string"), path + QStringLiteral(".key"));
        return std::nullopt;
    }
    const QString key = object.value(QStringLiteral("key")).toString();
    if (chordKeyLooksLikeShellOrPath(key)) {
        error = makeError(QStringLiteral("chord key looks like a shell command or path"), path + QStringLiteral(".key"));
        return std::nullopt;
    }
    if (!isAllowedChordKey(key)) {
        error = makeError(QStringLiteral("chord key is not in the allowed table"), path + QStringLiteral(".key"));
        return std::nullopt;
    }

    Chord chord;
    chord.key = key;
    if (object.contains(QStringLiteral("modifiers"))) {
        const QJsonValue modifiersValue = object.value(QStringLiteral("modifiers"));
        if (!modifiersValue.isArray()) {
            error = makeError(QStringLiteral("chord.modifiers must be an array"), path + QStringLiteral(".modifiers"));
            return std::nullopt;
        }
        const QJsonArray modifiers = modifiersValue.toArray();
        QSet<Modifier> seen;
        for (int i = 0; i < modifiers.size(); ++i) {
            const QJsonValue item = modifiers.at(i);
            if (!item.isString()) {
                error = makeError(QStringLiteral("modifier must be a string"),
                                  path + QStringLiteral(".modifiers[%1]").arg(i));
                return std::nullopt;
            }
            const auto modifier = modifierFromJsonName(item.toString());
            if (!modifier) {
                error = makeError(QStringLiteral("unknown modifier"), path + QStringLiteral(".modifiers[%1]").arg(i));
                return std::nullopt;
            }
            if (seen.contains(*modifier)) {
                error = makeError(QStringLiteral("duplicate modifier"), path + QStringLiteral(".modifiers[%1]").arg(i));
                return std::nullopt;
            }
            seen.insert(*modifier);
            chord.modifiers.push_back(*modifier);
        }
    }
    return chord;
}

std::optional<Assignment> parseAssignment(const QJsonObject &object, const QString &path, bool allowInherit,
                                          PersistenceError &error)
{
    if (!object.contains(QStringLiteral("action")) || !object.value(QStringLiteral("action")).isString()) {
        error = makeError(QStringLiteral("assignment.action must be a string"), path + QStringLiteral(".action"));
        return std::nullopt;
    }
    const QString actionName = object.value(QStringLiteral("action")).toString();
    const auto action = actionFromJsonName(actionName);
    if (!action) {
        error = makeError(QStringLiteral("unknown action type"), path + QStringLiteral(".action"));
        return std::nullopt;
    }

    QStringList allowed{QStringLiteral("action")};
    switch (*action) {
    case ActionType::EmitShortcut:
        allowed << QStringLiteral("chord");
        break;
    case ActionType::ApprovedSystemAction:
        allowed << QStringLiteral("action_id");
        break;
    default:
        break;
    }
    if (!checkObjectKeys(object, allowed, path, error)) {
        return std::nullopt;
    }

    if (*action == ActionType::InheritGlobal && !allowInherit) {
        error = makeError(QStringLiteral("inherit_global is valid only on an application profile"), path + QStringLiteral(".action"));
        return std::nullopt;
    }

    Assignment assignment;
    assignment.action = *action;

    if (*action == ActionType::EmitShortcut) {
        if (!object.contains(QStringLiteral("chord")) || !object.value(QStringLiteral("chord")).isObject()) {
            error = makeError(QStringLiteral("emit_shortcut requires a chord object"), path + QStringLiteral(".chord"));
            return std::nullopt;
        }
        const auto chord = parseChord(object.value(QStringLiteral("chord")).toObject(), path + QStringLiteral(".chord"), error);
        if (!chord) {
            return std::nullopt;
        }
        assignment.chord = *chord;
    } else if (*action == ActionType::ApprovedSystemAction) {
        if (!object.contains(QStringLiteral("action_id")) || !object.value(QStringLiteral("action_id")).isString()) {
            error = makeError(QStringLiteral("approved_system_action requires action_id"), path + QStringLiteral(".action_id"));
            return std::nullopt;
        }
        const auto systemAction = systemActionFromJsonName(object.value(QStringLiteral("action_id")).toString());
        if (!systemAction) {
            error = makeError(QStringLiteral("unknown approved system action"), path + QStringLiteral(".action_id"));
            return std::nullopt;
        }
        assignment.systemAction = *systemAction;
    }

    return assignment;
}

QJsonObject assignmentToJson(const Assignment &assignment)
{
    QJsonObject object;
    object.insert(QStringLiteral("action"), actionJsonName(assignment.action));
    if (assignment.action == ActionType::EmitShortcut && assignment.chord) {
        QJsonObject chord;
        chord.insert(QStringLiteral("key"), assignment.chord->key);
        QJsonArray modifiers;
        for (Modifier modifier : assignment.chord->modifiers) {
            modifiers.append(modifierJsonName(modifier));
        }
        chord.insert(QStringLiteral("modifiers"), modifiers);
        object.insert(QStringLiteral("chord"), chord);
    }
    if (assignment.action == ActionType::ApprovedSystemAction && assignment.systemAction) {
        object.insert(QStringLiteral("action_id"), systemActionJsonName(*assignment.systemAction));
    }
    return object;
}

} // namespace

bool parseKeys(const QJsonValue &value, const QString &path, bool allowInherit, QHash<ControlId, Assignment> &out,
               PersistenceError &error)
{
    if (!value.isObject()) {
        error = makeError(QStringLiteral("keys must be an object"), path);
        return false;
    }
    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        const auto control = controlFromJsonName(it.key());
        if (!control) {
            error = makeError(QStringLiteral("unknown control"), path + QLatin1Char('.') + it.key());
            return false;
        }
        if (!it.value().isObject()) {
            error = makeError(QStringLiteral("assignment must be an object"), path + QLatin1Char('.') + it.key());
            return false;
        }
        const auto assignment = parseAssignment(it.value().toObject(), path + QLatin1Char('.') + it.key(), allowInherit, error);
        if (!assignment) {
            return false;
        }
        if (controlIsConditional(*control)
            && (assignment->action == ActionType::EmitShortcut
                || assignment->action == ActionType::ApprovedSystemAction)) {
            error = makeError(QStringLiteral("GameMode and Backlight are conditional and cannot be bound in M1"),
                              path + QLatin1Char('.') + it.key());
            return false;
        }
        out.insert(*control, *assignment);
    }
    return true;
}

QJsonObject keysToJson(const QHash<ControlId, Assignment> &keys)
{
    QJsonObject object;
    for (const ControlInfo &info : controlCatalog()) {
        if (keys.contains(info.id)) {
            object.insert(QString::fromLatin1(info.jsonName), assignmentToJson(keys.value(info.id)));
        }
    }
    return object;
}

} // namespace contextdeck::persistence
