#include "app/SettingsHost.h"

#include "app/AppController.h"

#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QWindow>

namespace contextdeck {

Q_LOGGING_CATEGORY(lcSettings, "contextdeck.settings")

SettingsHost::SettingsHost(AppController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
}

void SettingsHost::show()
{
    if (m_engine == nullptr) {
        m_engine = new QQmlApplicationEngine(this);
        m_engine->rootContext()->setContextProperty(QStringLiteral("app"), m_controller);
        m_engine->loadFromModule(QStringLiteral("io.github.cisarik.ContextDeck"), QStringLiteral("Main"));
        if (m_engine->rootObjects().isEmpty()) {
            qCWarning(lcSettings) << "settings QML failed to load";
            return;
        }
        qCInfo(lcSettings) << "settings window created";
    }

    const auto roots = m_engine->rootObjects();
    for (QObject *object : roots) {
        if (auto *window = qobject_cast<QWindow *>(object)) {
            window->show();
            window->requestActivate();
            return;
        }
        if (auto *window = object->findChild<QWindow *>()) {
            window->show();
            window->requestActivate();
            return;
        }
    }
}

} // namespace contextdeck
