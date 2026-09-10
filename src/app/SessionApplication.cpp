#include "app/SessionApplication.h"

#include "context/DBusNames.h"

#include <QApplication>
#include <QLoggingCategory>

namespace contextdeck {

Q_LOGGING_CATEGORY(lcApp, "contextdeck.app")

SessionApplication::SessionApplication(QApplication *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_context(this)
{
    Q_UNUSED(m_app);
}

bool SessionApplication::start()
{
    const bool registered = m_context.start();
    if (registered) {
        qCInfo(lcApp) << "bus name registered:" << kServiceName;
    } else {
        qCWarning(lcApp) << "running degraded:" << m_context.lastError();
    }
    return registered;
}

} // namespace contextdeck
