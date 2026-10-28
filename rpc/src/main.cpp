// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// RiftWii-RPC: shows the game a Wii on this network is playing in RiftWii
// as Discord rich presence (docs/PRESENCE.md).
//
//   RiftWii-RPC                the window, and the tray icon
//   RiftWii-RPC --background   the tray icon only (as at login)
//   RiftWii-RPC --headless     no window or tray at all, a log on the
//                              terminal: for a machine without a desktop
//                              that runs Discord all the same
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QTextStream>

#include <cstdio>
#include <cstring>
#include <memory>

#include "app.hpp"
#include "mainwindow.hpp"

namespace {

using riftwii::rpc::App;
using riftwii::rpc::MainWindow;

QString instanceName() {
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty()) user = qEnvironmentVariable("USERNAME");
    return QStringLiteral("riftwii-rpc-") + user;
}

// One copy per user: a second one started (from the Start menu, say)
// shows the first one's window and ends.
bool otherCopyShown() {
    QLocalSocket socket;
    socket.connectToServer(instanceName());
    if (!socket.waitForConnected(300)) return false;
    socket.write("show\n");
    socket.waitForBytesWritten(300);
    return true;
}

void listenForOtherCopies(QObject* parent, MainWindow* window) {
    auto* server = new QLocalServer(parent);
    QLocalServer::removeServer(instanceName());  // left by a copy that crashed
    if (!server->listen(instanceName())) return;
    QObject::connect(server, &QLocalServer::newConnection, parent, [server, window] {
        while (QLocalSocket* socket = server->nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            if (window) window->bringToFront();
        }
    });
}

bool hasFlag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], flag) == 0) return true;
    return false;
}

void setIdentity() {
    QCoreApplication::setOrganizationName(QStringLiteral("RiftWii"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("riftwii.xyz"));
    QCoreApplication::setApplicationName(QStringLiteral("RiftWii-RPC"));
    QCoreApplication::setApplicationVersion(QStringLiteral(RIFTWII_RPC_VERSION));
}

struct Options {
    bool background = false;
    quint16 port = riftwii::kPresencePort;
    QString clientId;
};

Options parse(const QCoreApplication& app) {
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Discord rich presence for the game a Wii on this network plays in RiftWii."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption background(QStringLiteral("background"), QStringLiteral("Start in the tray, without the window."));
    const QCommandLineOption headless(QStringLiteral("headless"),
                                      QStringLiteral("No window or tray: log to the terminal."));
    const QCommandLineOption port(QStringLiteral("port"), QStringLiteral("The UDP port to listen on."),
                                  QStringLiteral("port"), QString::number(riftwii::kPresencePort));
    const QCommandLineOption clientId(QStringLiteral("client-id"),
                                      QStringLiteral("The Discord application ID, for this run only."),
                                      QStringLiteral("id"));
    parser.addOptions({background, headless, port, clientId});
    parser.process(app);
    Options out;
    out.background = parser.isSet(background);
    const uint wanted = parser.value(port).toUInt();
    if (wanted > 0 && wanted < 65536) out.port = static_cast<quint16>(wanted);
    out.clientId = parser.value(clientId);
    return out;
}

int runHeadless(int argc, char** argv) {
    QCoreApplication qt(argc, argv);
    setIdentity();
    const Options options = parse(qt);
    App app;
    app.setRunClientId(options.clientId);
    QTextStream out(stdout);
    QObject::connect(&app, &App::logLine, &qt, [&out](const QString& line) {
        out << line << Qt::endl;
    });
    QString error;
    if (!app.start(options.port, error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    return qt.exec();
}

}  // namespace

int main(int argc, char** argv) {
    if (hasFlag(argc, argv, "--headless")) return runHeadless(argc, argv);

    QApplication qt(argc, argv);
    setIdentity();
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/riftwii-rpc.png")));
    const Options options = parse(qt);
    if (otherCopyShown()) return 0;

    App app;
    app.setRunClientId(options.clientId);
    auto window = std::make_unique<MainWindow>(app);
    listenForOtherCopies(&app, window.get());
    QString error;
    const bool listening = app.start(options.port, error);
    const bool hidden = (options.background || app.settings().startHidden) && window->hasTray();
    if (!hidden || !listening) window->show();
    if (!listening) {
        QMessageBox::critical(window.get(), QStringLiteral("RiftWii-RPC"),
                              error + QStringLiteral("\n\nIs another program using it?"));
        return 1;
    }
    return qt.exec();
}
