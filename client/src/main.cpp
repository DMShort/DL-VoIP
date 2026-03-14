#include <QApplication>
#include <QCommandLineParser>
#include <cstdio>

#include "config/config.h"
#include "network/websocket_client.h"
#include "ui/login_dialog.h"
#include "ui/main_window.h"

void logToStderr(QtMsgType, const QMessageLogContext&, const QString& msg) {
    fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    fflush(stderr);
}

int main(int argc, char* argv[]) {
    qInstallMessageHandler(logToStderr);
    QApplication app(argc, argv);
    app.setApplicationName("DadLink");
    app.setApplicationVersion("2.0.0");
    app.setOrganizationName("DadLink");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    // Load configuration
    AppConfig config;
    config.load();

    // Create WebSocket client (lives for the entire application session)
    WebSocketClient wsClient;

    // Show login dialog — blocks until authenticated or cancelled
    LoginDialog loginDialog(config, &wsClient);
    if (loginDialog.exec() != QDialog::Accepted) {
        return 0;
    }

    // Build main window with auth results
    MainWindow mainWindow(config, &wsClient,
                          loginDialog.userId(),
                          loginDialog.token(),
                          loginDialog.voicePort(),
                          loginDialog.channels(),
                          loginDialog.users());
    mainWindow.show();

    return app.exec();
}
