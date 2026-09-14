#include "workspace/DesktopMutator.h"

#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QEventLoop>
#include <QTimer>
#include <QVariant>

#include <algorithm>

namespace contextdeck {

namespace {

const QString kKwinService = QStringLiteral("org.kde.KWin");
const QString kManagerPath = QStringLiteral("/VirtualDesktopManager");
const QString kManagerInterface = QStringLiteral("org.kde.KWin.VirtualDesktopManager");
const QString kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

QString observedIdAtOrdinal(const WorkspaceState &state, int ordinal)
{
    for (const WorkspaceDesktop &desktop : state.desktops) {
        if (desktop.ordinal == ordinal) {
            return desktop.id;
        }
    }
    return {};
}

bool observedContainsId(const WorkspaceState &state, const QString &id)
{
    if (id.isEmpty()) {
        return false;
    }
    for (const WorkspaceDesktop &desktop : state.desktops) {
        if (desktop.id == id) {
            return true;
        }
    }
    return false;
}

} // namespace

DesktopMutator::DesktopMutator(QObject *parent)
    : DesktopMutator(QDBusConnection::sessionBus(), parent)
{
}

DesktopMutator::DesktopMutator(const QDBusConnection &connection, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
{
}

void DesktopMutator::setCheckpointPath(const QString &path)
{
    m_checkpoint = WorkspaceCheckpoint(path);
}

void DesktopMutator::setCallTimeoutForTest(int timeoutMs)
{
    m_callTimeoutMs = std::max(1, timeoutMs);
}

void DesktopMutator::recordCreatedDesktop(const QString &id, int position)
{
    if (!m_applying || id.isEmpty() || !m_expectedCreatePositions.contains(position)) {
        return;
    }
    if (m_checkpointData.createdIds.contains(id)) {
        return;
    }
    m_checkpointData.createdIds.push_back(id);
    QString errorClass;
    if (!persistCheckpoint(errorClass)) {
        m_checkpointUpdateFailed = true;
    }
    emit desktopCreatedInTransaction(id, position);
}

DesktopMutator::CallOutcome DesktopMutator::call(const QDBusMessage &message)
{
    CallOutcome outcome;
    QDBusPendingCall pending = m_connection.asyncCall(message);
    QDBusPendingCallWatcher watcher(pending);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(m_callTimeoutMs);
    loop.exec();
    if (!watcher.isFinished()) {
        outcome.errorClass = QStringLiteral("mutation-timeout");
        return outcome;
    }
    outcome.reply = watcher.reply();
    if (outcome.reply.type() != QDBusMessage::ReplyMessage) {
        outcome.errorClass = QStringLiteral("mutation-error");
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

bool DesktopMutator::createDesktop(int position, const QString &name, QString &errorClass)
{
    QDBusMessage message =
        QDBusMessage::createMethodCall(kKwinService, kManagerPath, kManagerInterface, QStringLiteral("createDesktop"));
    message << static_cast<quint32>(position) << name;
    const CallOutcome outcome = call(message);
    if (!outcome.ok) {
        errorClass = outcome.errorClass.isEmpty() ? QStringLiteral("create-failed") : outcome.errorClass;
        return false;
    }
    if (m_checkpointUpdateFailed) {
        errorClass = QStringLiteral("checkpoint-update");
        return false;
    }
    return true;
}

bool DesktopMutator::setDesktopName(const QString &id, const QString &name, QString &errorClass)
{
    QDBusMessage message = QDBusMessage::createMethodCall(kKwinService, kManagerPath, kManagerInterface,
                                                          QStringLiteral("setDesktopName"));
    message << id << name;
    const CallOutcome outcome = call(message);
    if (!outcome.ok) {
        errorClass = outcome.errorClass.isEmpty() ? QStringLiteral("rename-failed") : outcome.errorClass;
        return false;
    }
    return true;
}

bool DesktopMutator::removeDesktop(const QString &id, QString &errorClass)
{
    QDBusMessage message =
        QDBusMessage::createMethodCall(kKwinService, kManagerPath, kManagerInterface, QStringLiteral("removeDesktop"));
    message << id;
    const CallOutcome outcome = call(message);
    if (!outcome.ok) {
        errorClass = outcome.errorClass.isEmpty() ? QStringLiteral("remove-failed") : outcome.errorClass;
        return false;
    }
    return true;
}

bool DesktopMutator::setUintProperty(const QString &name, quint32 value, QString &errorClass)
{
    QDBusMessage message =
        QDBusMessage::createMethodCall(kKwinService, kManagerPath, kPropertiesInterface, QStringLiteral("Set"));
    message << kManagerInterface << name << QVariant::fromValue(QDBusVariant(QVariant::fromValue(value)));
    const CallOutcome outcome = call(message);
    if (!outcome.ok) {
        errorClass = outcome.errorClass.isEmpty() ? QStringLiteral("property-failed") : outcome.errorClass;
        return false;
    }
    return true;
}

bool DesktopMutator::setBoolProperty(const QString &name, bool value, QString &errorClass)
{
    QDBusMessage message =
        QDBusMessage::createMethodCall(kKwinService, kManagerPath, kPropertiesInterface, QStringLiteral("Set"));
    message << kManagerInterface << name << QVariant::fromValue(QDBusVariant(QVariant::fromValue(value)));
    const CallOutcome outcome = call(message);
    if (!outcome.ok) {
        errorClass = outcome.errorClass.isEmpty() ? QStringLiteral("property-failed") : outcome.errorClass;
        return false;
    }
    return true;
}

bool DesktopMutator::setStringProperty(const QString &name, const QString &value, QString &errorClass)
{
    QDBusMessage message =
        QDBusMessage::createMethodCall(kKwinService, kManagerPath, kPropertiesInterface, QStringLiteral("Set"));
    message << kManagerInterface << name << QVariant::fromValue(QDBusVariant(QVariant::fromValue(value)));
    const CallOutcome outcome = call(message);
    if (!outcome.ok) {
        errorClass = outcome.errorClass.isEmpty() ? QStringLiteral("property-failed") : outcome.errorClass;
        return false;
    }
    return true;
}

bool DesktopMutator::persistCheckpoint(QString &errorClass)
{
    if (!m_checkpoint.save(m_checkpointData)) {
        errorClass = QStringLiteral("checkpoint-write");
        return false;
    }
    if (!m_checkpoint.exists()) {
        errorClass = QStringLiteral("checkpoint-verify");
        return false;
    }
    return true;
}

WorkspaceMutationResult DesktopMutator::apply(const WorkspacePlan &plan, const WorkspaceState &observed,
                                              const WorkspaceMutationOptions &options)
{
    WorkspaceMutationResult result;
    if (m_applying) {
        result.failureClass = QStringLiteral("apply-in-progress");
        return result;
    }
    m_applying = true;
    m_checkpointUpdateFailed = false;
    m_expectedCreatePositions.clear();

    QVector<WorkspaceDesktopPlan> creates;
    QVector<WorkspaceDesktopPlan> renames;
    for (const WorkspaceDesktopPlan &desktop : plan.desktops) {
        if (desktop.create) {
            creates.push_back(desktop);
        } else if (desktop.rename) {
            renames.push_back(desktop);
        }
    }
    std::sort(creates.begin(), creates.end(),
              [](const WorkspaceDesktopPlan &left, const WorkspaceDesktopPlan &right) {
                  return left.ordinal < right.ordinal;
              });
    std::sort(renames.begin(), renames.end(),
              [](const WorkspaceDesktopPlan &left, const WorkspaceDesktopPlan &right) {
                  return left.ordinal < right.ordinal;
              });

    QVector<WorkspaceDesktop> extras;
    if (options.removeExtras) {
        for (const WorkspaceDesktop &desktop : observed.desktops) {
            if (desktop.ordinal > plan.desiredDesktopCount) {
                extras.push_back(desktop);
            }
        }
        std::sort(extras.begin(), extras.end(),
                  [](const WorkspaceDesktop &left, const WorkspaceDesktop &right) {
                      return left.ordinal < right.ordinal;
                  });
    }

    const bool anyMutation = !creates.isEmpty() || !renames.isEmpty() || plan.rowsChange || plan.wrappingChange
        || options.switchCurrent || !extras.isEmpty();
    if (!anyMutation) {
        result.ok = true;
        result.noChanges = true;
        m_applying = false;
        return result;
    }

    m_checkpointData = checkpointFromState(observed);
    m_checkpointData.createdIds.clear();
    for (const WorkspaceDesktopPlan &desktop : creates) {
        m_expectedCreatePositions.push_back(desktop.ordinal - 1);
    }

    QString errorClass;
    if (!persistCheckpoint(errorClass)) {
        result.failureClass = errorClass;
        m_applying = false;
        m_expectedCreatePositions.clear();
        return result;
    }

    const auto failApply = [&](const QString &cls) {
        const QVector<QString> createdIds = m_checkpointData.createdIds;
        const WorkspaceMutationResult revertResult = runRevert(observed, cls);
        result.ok = false;
        result.failureClass = cls;
        result.reverted = revertResult.reverted;
        result.revertFailed = revertResult.revertFailed;
        result.residualClass = revertResult.residualClass;
        result.createdIds = createdIds;
        m_applying = false;
        m_expectedCreatePositions.clear();
        return result;
    };

    for (const WorkspaceDesktopPlan &desktop : creates) {
        if (!createDesktop(desktop.ordinal - 1, desktop.name, errorClass)) {
            return failApply(errorClass);
        }
        ++result.createdCount;
    }

    for (const WorkspaceDesktopPlan &desktop : renames) {
        const QString id = observedIdAtOrdinal(observed, desktop.ordinal);
        if (id.isEmpty() || !setDesktopName(id, desktop.name, errorClass)) {
            if (errorClass.isEmpty()) {
                errorClass = QStringLiteral("rename-id-missing");
            }
            return failApply(errorClass);
        }
        ++result.renamedCount;
    }

    if (plan.rowsChange && plan.desiredRows.has_value()) {
        if (!setUintProperty(QStringLiteral("rows"), static_cast<quint32>(*plan.desiredRows), errorClass)) {
            return failApply(errorClass);
        }
        result.rowsChanged = true;
    }

    if (plan.wrappingChange && plan.desiredWrapping.has_value()) {
        if (!setBoolProperty(QStringLiteral("navigationWrappingAround"), *plan.desiredWrapping, errorClass)) {
            return failApply(errorClass);
        }
        result.wrappingChanged = true;
    }

    if (options.switchCurrent) {
        const QString target = observedIdAtOrdinal(observed, 1);
        if (target.isEmpty()) {
            errorClass = QStringLiteral("current-target-missing");
            return failApply(errorClass);
        }
        if (!setStringProperty(QStringLiteral("current"), target, errorClass)) {
            return failApply(errorClass);
        }
        result.currentSwitched = true;
    }

    if (options.removeExtras) {
        for (int i = extras.size() - 1; i >= 0; --i) {
            if (!removeDesktop(extras.at(i).id, errorClass)) {
                return failApply(errorClass);
            }
            ++result.removedCount;
        }
    }

    result.ok = true;
    result.createdIds = m_checkpointData.createdIds;
    m_applying = false;
    m_expectedCreatePositions.clear();
    return result;
}

WorkspaceMutationResult DesktopMutator::revert(const WorkspaceState &observed)
{
    WorkspaceMutationResult result;
    if (m_applying) {
        result.failureClass = QStringLiteral("apply-in-progress");
        return result;
    }
    return runRevert(observed, QString());
}

WorkspaceMutationResult DesktopMutator::runRevert(const WorkspaceState &observed, const QString &failureClass)
{
    WorkspaceMutationResult result;
    result.failureClass = failureClass;
    WorkspaceCheckpointData data;
    if (!m_checkpointData.desktops.isEmpty()) {
        data = m_checkpointData;
    } else {
        const std::optional<WorkspaceCheckpointData> loaded = m_checkpoint.load();
        if (!loaded.has_value()) {
            result.reverted = false;
            result.revertFailed = true;
            result.residualClass = QStringLiteral("checkpoint-missing");
            return result;
        }
        data = *loaded;
    }
    result.reverted = true;

    int hardResiduals = 0;
    QString firstHardResidual;
    QString firstSoftResidual;
    for (auto it = data.createdIds.crbegin(); it != data.createdIds.crend(); ++it) {
        QString errorClass;
        if (!removeDesktop(*it, errorClass)) {
            ++hardResiduals;
            if (firstHardResidual.isEmpty()) {
                firstHardResidual = errorClass;
            }
        }
    }

    for (const WorkspaceCheckpointDesktop &desktop : data.desktops) {
        if (observed.availability == WorkspaceAvailability::Available && !observedContainsId(observed, desktop.id)) {
            continue;
        }
        QString errorClass;
        if (!setDesktopName(desktop.id, desktop.name, errorClass)) {
            ++hardResiduals;
            if (firstHardResidual.isEmpty()) {
                firstHardResidual = errorClass;
            }
        }
    }

    if (data.rows.has_value()) {
        QString errorClass;
        if (!setUintProperty(QStringLiteral("rows"), static_cast<quint32>(*data.rows), errorClass)) {
            ++hardResiduals;
            if (firstHardResidual.isEmpty()) {
                firstHardResidual = errorClass;
            }
        }
    }
    if (data.wrapping.has_value()) {
        QString errorClass;
        if (!setBoolProperty(QStringLiteral("navigationWrappingAround"), *data.wrapping, errorClass)) {
            ++hardResiduals;
            if (firstHardResidual.isEmpty()) {
                firstHardResidual = errorClass;
            }
        }
    }

    if (!data.currentId.isEmpty()) {
        const bool knownAbsent = observed.availability == WorkspaceAvailability::Available
            && !observedContainsId(observed, data.currentId);
        if (knownAbsent) {
            firstSoftResidual = QStringLiteral("current-restore-skipped");
        } else {
            QString errorClass;
            if (!setStringProperty(QStringLiteral("current"), data.currentId, errorClass)) {
                firstSoftResidual = QStringLiteral("current-restore-skipped");
            }
        }
    }

    if (hardResiduals > 0) {
        result.revertFailed = true;
        result.residualClass = firstHardResidual;
        result.ok = false;
        return result;
    }
    result.revertFailed = false;
    result.residualClass = firstSoftResidual;
    result.ok = true;
    m_checkpoint.remove();
    m_checkpointData = {};
    return result;
}

} // namespace contextdeck
