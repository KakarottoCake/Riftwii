// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHostAddress>
#include <QObject>
#include <QUdpSocket>

#include "riftwii/presence.hpp"

namespace riftwii::rpc {

// RiftWii's messages from the local network (riftwii/presence.hpp): the
// Wii sends them to everyone, so nothing here has to find the Wii first.
// The port is shared, so two copies of the app (or another listener)
// both hear them.
class Listener : public QObject {
    Q_OBJECT
public:
    explicit Listener(QObject* parent = nullptr);
    // False with `error` when the port cannot be had.
    bool start(quint16 port, QString& error);
    quint16 port() const { return socket_.localPort(); }

signals:
    void received(const riftwii::PresenceMessage& message, const QHostAddress& from);

private:
    void onReadyRead();
    QUdpSocket socket_;
};

}  // namespace riftwii::rpc

Q_DECLARE_METATYPE(riftwii::PresenceMessage)
