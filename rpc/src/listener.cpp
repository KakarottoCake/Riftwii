// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "listener.hpp"

#include <QNetworkDatagram>

namespace riftwii::rpc {

Listener::Listener(QObject* parent) : QObject(parent) {
    connect(&socket_, &QUdpSocket::readyRead, this, &Listener::onReadyRead);
}

bool Listener::start(quint16 port, QString& error) {
    socket_.close();
    if (!socket_.bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        error = QStringLiteral("Cannot listen on UDP port %1: %2").arg(port).arg(socket_.errorString());
        return false;
    }
    return true;
}

void Listener::onReadyRead() {
    while (socket_.hasPendingDatagrams()) {
        const QNetworkDatagram datagram = socket_.receiveDatagram(kPresenceMaxBytes + 1);
        const QByteArray data = datagram.data();
        // Anyone on the network can send here: only a whole, well-formed
        // message counts, and the parser bounds every field.
        if (data.size() > static_cast<int>(kPresenceMaxBytes)) continue;
        PresenceMessage message;
        if (!parse_presence_packet(data.toStdString(), message)) continue;
        emit received(message, datagram.senderAddress());
    }
}

}  // namespace riftwii::rpc
