#include "app/TrayController.h"

#include "app/AppController.h"

#include <KStatusNotifierItem>
#include <QAction>
#include <QApplication>
#include <QLoggingCategory>
#include <QMenu>
#include <QMessageBox>

namespace contextdeck {

Q_LOGGING_CATEGORY(lcTray, "contextdeck.tray")

TrayController::TrayController(AppController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
}

void TrayController::start()
{
    m_item = new KStatusNotifierItem(QStringLiteral("contextdeck"), this);
    m_item->setTitle(QStringLiteral("ContextDeck"));
    m_item->setCategory(KStatusNotifierItem::Hardware);
    m_item->setStatus(KStatusNotifierItem::Active);
    m_item->setIconByName(QStringLiteral("input-keyboard"));
    m_item->setToolTip(QStringLiteral("input-keyboard"), QStringLiteral("ContextDeck"),
                       QStringLiteral("G213 context lighting"));
    m_item->setStandardActionsEnabled(false);

    m_menu = new QMenu();
    m_item->setContextMenu(m_menu);
    rebuildMenu();

    connect(m_item, &KStatusNotifierItem::activateRequested, this, [this](bool, const QPoint &) {
        emit showSettingsRequested();
    });
    connect(m_controller, &AppController::contextChanged, this, &TrayController::rebuildMenu);
    connect(m_controller, &AppController::lightingModeChanged, this, &TrayController::rebuildMenu);
    connect(m_controller, &AppController::diagnosticsChanged, this, &TrayController::rebuildMenu);
    qCInfo(lcTray) << "status notifier started";
}

void TrayController::rebuildMenu()
{
    if (m_menu == nullptr) {
        return;
    }
    m_menu->clear();
    m_menu->addAction(QStringLiteral("App: %1").arg(m_controller->currentApplication()))->setEnabled(false);
    m_menu->addAction(QStringLiteral("Profile: %1").arg(m_controller->currentProfile()))->setEnabled(false);
    const QString mode = m_controller->lightingMode();
    m_menu->addAction(QStringLiteral("Lighting: %1 (%2)").arg(mode, m_controller->lightingConnection()))->setEnabled(false);
    m_menu->addSeparator();

    auto *automatic = m_menu->addAction(QStringLiteral("Automatic"));
    automatic->setCheckable(true);
    automatic->setChecked(mode == QLatin1String("automatic"));
    connect(automatic, &QAction::triggered, this, [this](bool checked) {
        if (checked) {
            m_controller->restoreAutomatic();
        }
    });

    auto *lightsOff = m_menu->addAction(QStringLiteral("Lights off"));
    lightsOff->setCheckable(true);
    lightsOff->setChecked(mode == QLatin1String("lights_off"));
    connect(lightsOff, &QAction::triggered, this, [this](bool checked) {
        if (checked) {
            m_controller->lightsOff();
        } else {
            m_controller->restoreAutomatic();
        }
    });

    m_menu->addAction(QStringLiteral("Restore automatic"), this, [this]() { m_controller->restoreAutomatic(); });
    m_menu->addSeparator();
    m_menu->addAction(QStringLiteral("Displays Off"), this, [this]() { m_controller->displaysOff(); });
    m_menu->addAction(QStringLiteral("Suspend…"), this, [this]() { confirmSuspend(); });
    m_menu->addSeparator();
    m_menu->addAction(QStringLiteral("Settings…"), this, [this]() { emit showSettingsRequested(); });
    m_menu->addAction(QStringLiteral("Quit"), qApp, &QApplication::quit);
}

void TrayController::confirmSuspend()
{
    const auto result = QMessageBox::question(nullptr, QStringLiteral("ContextDeck"),
                                              QStringLiteral("Suspend this computer now?"),
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (result == QMessageBox::Yes) {
        m_controller->suspend();
    }
}

} // namespace contextdeck
