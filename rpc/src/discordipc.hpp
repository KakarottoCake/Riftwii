// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace riftwii::rpc {

// The Discord app's local rich presence socket, as Discord's own SDK talks
// to it: a named pipe on Windows (\\.\pipe\discord-ipc-N), a Unix socket
// elsewhere (discord-ipc-N in the temporary folder, or in Flatpak's and
// Snap's). Each message is a little-endian opcode and length, then JSON.
// Keeps trying while Discord is closed, and sends the last activity again
// once it is back.
class DiscordIpc : public QObject {
    Q_OBJECT
public:
    explicit DiscordIpc(QObject* parent = nullptr);
    ~DiscordIpc() override;

    // Connects with this application ID (and again whenever it changes).
    void start(const QString& clientId);
    void stop();
    // The activity Discord shows; an empty object clears it.
    void setActivity(const QJsonObject& activity);
    bool isReady() const { return ready_; }
    QString userName() const { return userName_; }

    // Where Discord's socket may be, in the order tried.
    static QStringList candidatePaths();
    // One message as Discord frames it.
    static QByteArray frame(quint32 opcode, const QJsonObject& body);

signals:
    void readyChanged(bool ready);
    void problem(const QString& text);

private:
    enum Opcode : quint32 { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

    void tryConnect();
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void handle(quint32 opcode, const QJsonObject& body);
    void sendActivity();
    void setReady(bool ready);

    QLocalSocket* socket_ = nullptr;
    QTimer retry_;
    QStringList paths_;
    int pathIndex_ = 0;
    QString clientId_;
    QByteArray buffer_;
    QJsonObject activity_;
    bool ready_ = false;
    bool wanted_ = false;
    QString userName_;
};

}  // namespace riftwii::rpc
