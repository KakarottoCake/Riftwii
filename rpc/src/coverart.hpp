// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>

namespace riftwii::rpc {

// The game's cover on GameTDB, for the presence's big picture. Discord
// fetches the picture itself from the link; this only finds a link that
// exists, by asking GameTDB for each likely one in turn (its headers, not
// the picture), as RiiTag-RPC does.
class CoverArt : public QObject {
    Q_OBJECT
public:
    explicit CoverArt(QObject* parent = nullptr);
    // Looks the game up (once: the answer is kept). `found` follows, with
    // an empty link when GameTDB has no cover for it.
    void find(const QString& gameId);

    // The links tried for a game, best first: its own region's covers,
    // then English, US and Japanese ones, front covers before discs;
    // a channel's under its four letters after its six.
    static QStringList candidates(const QString& gameId);
    // GameTDB's region folder for a game's region letter ("RMCP01": EN).
    static QString regionFor(const QString& gameId);

signals:
    void found(const QString& gameId, const QString& url);

private:
    void tryNext(const QString& gameId, QStringList left);

    QNetworkAccessManager network_;
    QHash<QString, QString> known_;
    QStringList busy_;
};

}  // namespace riftwii::rpc
