#pragma once

#include "actions/PowerActions.h"
#include "app/AppController.h"
#include "app/SettingsHost.h"
#include "app/TrayController.h"
#include "context/ContextReceiver.h"
#include "rgb/OpenRgbClient.h"

#include <QObject>

class QApplication;

namespace contextdeck {

class SessionApplication : public QObject
{
    Q_OBJECT

public:
    explicit SessionApplication(QApplication *app, QObject *parent = nullptr);
    bool start();

    [[nodiscard]] ContextReceiver &context() { return m_context; }

private:
    QApplication *m_app = nullptr;
    ContextReceiver m_context;
    OpenRgbClient m_rgb;
    PowerActions m_power;
    AppController m_controller;
    TrayController m_tray;
    SettingsHost m_settings;
};

} // namespace contextdeck
