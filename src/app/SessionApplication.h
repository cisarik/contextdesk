#pragma once

#include "context/ContextReceiver.h"

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
    [[nodiscard]] const ContextReceiver &context() const { return m_context; }

private:
    QApplication *m_app = nullptr;
    ContextReceiver m_context;
};

} // namespace contextdeck
