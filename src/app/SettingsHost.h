#pragma once

#include <QObject>
#include <QPointer>

class QQmlApplicationEngine;
class QWindow;

namespace contextdeck {

class AppController;

class SettingsHost : public QObject
{
    Q_OBJECT

public:
    SettingsHost(AppController *controller, QObject *parent = nullptr);
    void show();

private:
    AppController *m_controller = nullptr;
    QQmlApplicationEngine *m_engine = nullptr;
};

} // namespace contextdeck
