#include "app/SessionApplication.h"

#include "context/DBusNames.h"

#include <QApplication>
#include <QGuiApplication>
#include <QLoggingCategory>

namespace contextdeck {

Q_LOGGING_CATEGORY(lcApp, "contextdeck.app")

SessionApplication::SessionApplication(QApplication *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_context(this)
    , m_rgb(this)
    , m_power(this)
    , m_controller(&m_context, &m_rgb, &m_power, this)
    , m_tray(&m_controller, this)
    , m_settings(&m_controller, this)
    , m_brokerIpc(this)
{
    Q_UNUSED(m_app);
    connect(&m_tray, &TrayController::showSettingsRequested, &m_settings, &SettingsHost::show);
}

bool SessionApplication::start()
{
    const bool registered = m_context.start();
    if (registered) {
        qCInfo(lcApp) << "bus name registered:" << kServiceName;
    } else {
        qCWarning(lcApp) << "running degraded:" << m_context.lastError();
    }
    m_controller.load();
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        m_settings.show();
    } else {
        m_rgb.start();
    }
    m_tray.start();
    m_brokerIpc.start();
    return registered;
}

} // namespace contextdeck
