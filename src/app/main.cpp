#include "app/SessionApplication.h"

#include <csignal>

#include <QApplication>
#include <QLoggingCategory>

namespace {

QApplication *g_app = nullptr;

void handleTerminate(int)
{
    if (g_app != nullptr) {
        g_app->quit();
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    g_app = &app;
    std::signal(SIGTERM, handleTerminate);
    std::signal(SIGINT, handleTerminate);

    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName(QStringLiteral("contextdeck"));
    app.setApplicationDisplayName(QStringLiteral("ContextDeck"));
    app.setOrganizationName(QStringLiteral("cisarik"));
    app.setOrganizationDomain(QStringLiteral("io.github.cisarik"));
    app.setDesktopFileName(QStringLiteral("io.github.cisarik.ContextDeck"));

    qputenv("QT_FORCE_STDERR_LOGGING", QByteArrayLiteral("1"));
    QLoggingCategory::setFilterRules(QStringLiteral("contextdeck.*.debug=false\ncontextdeck.*=true"));
    qSetMessagePattern(QStringLiteral("%{time} [%{category}] %{if-debug}D%{endif}%{if-info}I%{endif}%{if-warning}W%{endif}%{if-critical}C%{endif}: %{message}"));

    contextdeck::SessionApplication session(&app);
    session.start();
    return app.exec();
}
