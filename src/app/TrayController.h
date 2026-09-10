#pragma once

#include <QObject>
#include <QPointer>

class KStatusNotifierItem;
class QAction;
class QMenu;

namespace contextdeck {

class AppController;

class TrayController : public QObject
{
    Q_OBJECT

public:
    TrayController(AppController *controller, QObject *parent = nullptr);
    void start();

signals:
    void showSettingsRequested();

private:
    void rebuildMenu();
    void confirmSuspend();

    AppController *m_controller = nullptr;
    KStatusNotifierItem *m_item = nullptr;
    QMenu *m_menu = nullptr;
};

} // namespace contextdeck
