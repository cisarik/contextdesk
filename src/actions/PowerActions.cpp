#include "actions/PowerActions.h"

#include <KScreenDpms/Dpms>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDateTime>
#include <QLoggingCategory>

namespace contextdeck {

Q_LOGGING_CATEGORY(lcActions, "contextdeck.actions")

namespace {
constexpr qint64 kDisplaysOffDebounceMs = 2000;
constexpr qint64 kSuspendDebounceMs = 5000;
} // namespace

PowerActions::PowerActions(QObject *parent)
    : QObject(parent)
{
}

bool PowerActions::displaysOffSupported() const
{
    KScreen::Dpms dpms;
    return dpms.isSupported();
}

bool PowerActions::suspendAllowed() const
{
    QDBusInterface login1(QStringLiteral("org.freedesktop.login1"),
                          QStringLiteral("/org/freedesktop/login1"),
                          QStringLiteral("org.freedesktop.login1.Manager"),
                          QDBusConnection::systemBus());
    if (!login1.isValid()) {
        return false;
    }
    const QDBusReply<QString> reply = login1.call(QStringLiteral("CanSuspend"));
    return reply.isValid() && reply.value() == QLatin1String("yes");
}

bool PowerActions::displaysOff()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastDisplaysOffMs < kDisplaysOffDebounceMs) {
        qCInfo(lcActions) << "displays_off debounced";
        return false;
    }
    KScreen::Dpms dpms;
    if (!dpms.isSupported()) {
        m_lastError = QStringLiteral("DPMS is not supported");
        emit lastErrorChanged();
        qCWarning(lcActions) << m_lastError;
        return false;
    }
    dpms.switchMode(KScreen::Dpms::Off);
    m_lastDisplaysOffMs = now;
    qCInfo(lcActions) << "displays_off requested";
    return true;
}

bool PowerActions::suspend()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastSuspendMs < kSuspendDebounceMs) {
        qCInfo(lcActions) << "suspend debounced";
        return false;
    }
    if (!suspendAllowed()) {
        m_lastError = QStringLiteral("logind CanSuspend is not yes");
        emit lastErrorChanged();
        qCWarning(lcActions) << m_lastError;
        return false;
    }
    QDBusInterface login1(QStringLiteral("org.freedesktop.login1"),
                          QStringLiteral("/org/freedesktop/login1"),
                          QStringLiteral("org.freedesktop.login1.Manager"),
                          QDBusConnection::systemBus());
    const QDBusReply<void> reply = login1.call(QStringLiteral("Suspend"), false);
    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        emit lastErrorChanged();
        qCWarning(lcActions) << "suspend failed:" << m_lastError;
        return false;
    }
    m_lastSuspendMs = now;
    qCInfo(lcActions) << "suspend requested";
    return true;
}

} // namespace contextdeck
