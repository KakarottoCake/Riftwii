// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "discordipc.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>
#include <QtEndian>

namespace riftwii::rpc {
namespace {

// Discord numbers its sockets 0 to 9: one per Discord running (stable,
// PTB, Canary), the first free one taken.
constexpr int kSockets = 10;
// While Discord is closed, how often it is looked for.
constexpr int kRetryMs = 15 * 1000;
// Discord's own limit for one message is 64 KiB; an activity is far less.
constexpr qint64 kMaxFrame = 64 * 1024;

}  // namespace

DiscordIpc::DiscordIpc(QObject* parent) : QObject(parent) {
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, [this] {
        pathIndex_ = 0;
        paths_ = candidatePaths();
        tryConnect();
    });
}

DiscordIpc::~DiscordIpc() { stop(); }

QStringList DiscordIpc::candidatePaths() {
    QStringList out;
#if defined(Q_OS_WIN)
    // QLocalSocket puts \\.\pipe\ in front of a plain name.
    for (int i = 0; i < kSockets; ++i) out << QStringLiteral("discord-ipc-%1").arg(i);
#else
    QStringList bases;
    for (const char* name : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        const QString value = qEnvironmentVariable(name);
        if (!value.isEmpty() && !bases.contains(value)) bases << value;
    }
    if (!bases.contains("/tmp")) bases << "/tmp";
    // Discord packaged as a Flatpak or a Snap keeps its socket in a folder
    // of its own under the same base.
    const QStringList folders = {"", "app/com.discordapp.Discord/", "app/com.discordapp.DiscordCanary/",
                                 "snap.discord/", "snap.discord-canary/"};
    for (const QString& base : bases) {
        for (const QString& folder : folders) {
            for (int i = 0; i < kSockets; ++i) {
                const QString path = QStringLiteral("%1/%2discord-ipc-%3").arg(base, folder).arg(i);
                if (QFileInfo::exists(path)) out << path;
            }
        }
    }
#endif
    return out;
}

QByteArray DiscordIpc::frame(quint32 opcode, const QJsonObject& body) {
    const QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QByteArray out(8, '\0');
    qToLittleEndian<quint32>(opcode, out.data());
    qToLittleEndian<quint32>(static_cast<quint32>(json.size()), out.data() + 4);
    return out + json;
}

void DiscordIpc::start(const QString& clientId) {
    if (clientId == clientId_ && wanted_) return;
    stop();
    clientId_ = clientId;
    wanted_ = !clientId.isEmpty();
    if (!wanted_) return;
    pathIndex_ = 0;
    paths_ = candidatePaths();
    tryConnect();
}

void DiscordIpc::stop() {
    wanted_ = false;
    retry_.stop();
    if (socket_) {
        // Said goodbye, so Discord drops the activity at once.
        if (socket_->state() == QLocalSocket::ConnectedState) {
            socket_->write(frame(Close, {}));
            socket_->flush();
        }
        socket_->disconnect(this);
        socket_->abort();
        socket_->deleteLater();
        socket_ = nullptr;
    }
    buffer_.clear();
    setReady(false);
}

void DiscordIpc::tryConnect() {
    retry_.stop();
    if (!wanted_) return;
    if (pathIndex_ >= paths_.size()) {
        retry_.start(kRetryMs);
        return;
    }
    if (socket_) socket_->deleteLater();
    socket_ = new QLocalSocket(this);
    connect(socket_, &QLocalSocket::connected, this, &DiscordIpc::onConnected);
    connect(socket_, &QLocalSocket::readyRead, this, &DiscordIpc::onReadyRead);
    connect(socket_, &QLocalSocket::disconnected, this, &DiscordIpc::onDisconnected);
    QLocalSocket* attempt = socket_;
    connect(socket_, &QLocalSocket::errorOccurred, this, [this, attempt](QLocalSocket::LocalSocketError) {
        // Not this socket (a Discord that closed left it behind): the next.
        // Once ready, a lost connection is onDisconnected's to handle.
        if (attempt != socket_ || ready_) return;
        ++pathIndex_;
        QTimer::singleShot(0, this, &DiscordIpc::tryConnect);
    });
    socket_->connectToServer(paths_.at(pathIndex_));
}

void DiscordIpc::onConnected() {
    buffer_.clear();
    socket_->write(frame(Handshake, {{"v", 1}, {"client_id", clientId_}}));
}

void DiscordIpc::onReadyRead() {
    buffer_ += socket_->readAll();
    while (buffer_.size() >= 8) {
        const quint32 opcode = qFromLittleEndian<quint32>(buffer_.constData());
        const quint32 length = qFromLittleEndian<quint32>(buffer_.constData() + 4);
        if (length > kMaxFrame) {
            emit problem(QStringLiteral("Discord sent a message too large to be one of its own; reconnecting."));
            onDisconnected();
            return;
        }
        if (static_cast<quint32>(buffer_.size()) < 8 + length) return;
        const QJsonObject body = QJsonDocument::fromJson(buffer_.mid(8, length)).object();
        buffer_.remove(0, 8 + length);
        handle(opcode, body);
        if (!socket_) return;  // handle() may have closed it
    }
}

void DiscordIpc::handle(quint32 opcode, const QJsonObject& body) {
    if (opcode == Ping) {
        socket_->write(frame(Pong, body));
        return;
    }
    if (opcode == Close) {
        // Discord refused us: most often an application ID it does not know.
        const QString message = body.value("message").toString();
        emit problem(QStringLiteral("Discord closed the connection: %1")
                         .arg(message.isEmpty() ? QStringLiteral("no reason given") : message));
        const bool keep = wanted_;
        stop();
        wanted_ = keep;
        if (keep) retry_.start(kRetryMs);
        return;
    }
    if (opcode != Frame) return;
    const QString cmd = body.value("cmd").toString();
    const QString evt = body.value("evt").toString();
    if (cmd == "DISPATCH" && evt == "READY") {
        const QJsonObject user = body.value("data").toObject().value("user").toObject();
        userName_ = user.value("global_name").toString();
        if (userName_.isEmpty()) userName_ = user.value("username").toString();
        setReady(true);
        sendActivity();
    } else if (evt == "ERROR") {
        emit problem(QStringLiteral("Discord did not take the presence: %1")
                         .arg(body.value("data").toObject().value("message").toString()));
    }
}

void DiscordIpc::onDisconnected() {
    const bool wasReady = ready_;
    if (socket_) {
        socket_->disconnect(this);
        socket_->deleteLater();
        socket_ = nullptr;
    }
    buffer_.clear();
    setReady(false);
    if (!wanted_) return;
    // Discord closed or restarted: look for it again from the first socket.
    if (wasReady) emit problem(QStringLiteral("Discord closed; waiting for it to come back."));
    retry_.start(wasReady ? 2000 : kRetryMs);
}

void DiscordIpc::setActivity(const QJsonObject& activity) {
    activity_ = activity;
    sendActivity();
}

void DiscordIpc::sendActivity() {
    if (!ready_ || !socket_) return;
    QJsonObject args{{"pid", static_cast<qint64>(QCoreApplication::applicationPid())}};
    // A null activity is how Discord's SDK clears it.
    args.insert("activity", activity_.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(activity_));
    const QJsonObject body{{"cmd", "SET_ACTIVITY"},
                           {"args", args},
                           {"nonce", QUuid::createUuid().toString(QUuid::WithoutBraces)}};
    socket_->write(frame(Frame, body));
}

void DiscordIpc::setReady(bool ready) {
    if (ready_ == ready) return;
    ready_ = ready;
    if (!ready) userName_.clear();
    emit readyChanged(ready);
}

}  // namespace riftwii::rpc
