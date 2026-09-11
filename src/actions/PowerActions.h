#pragma once

#include <KScreenDpms/Dpms>
#include <QObject>

namespace contextdeck {

class PowerActions : public QObject
{
    Q_OBJECT

public:
    explicit PowerActions(QObject *parent = nullptr);

    [[nodiscard]] bool displaysOffSupported() const;
    [[nodiscard]] bool suspendAllowed() const;
    [[nodiscard]] QString lastError() const { return m_lastError; }

    bool displaysOff();
    bool suspend();

signals:
    void lastErrorChanged();

private:
    QString m_lastError;
    qint64 m_lastDisplaysOffMs = 0;
    qint64 m_lastSuspendMs = 0;
    KScreen::Dpms m_dpms;
};

} // namespace contextdeck
