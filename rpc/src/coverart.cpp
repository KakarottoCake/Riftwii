// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "coverart.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>

namespace riftwii::rpc {

CoverArt::CoverArt(QObject* parent) : QObject(parent) {}

QString CoverArt::regionFor(const QString& gameId) {
    if (gameId.size() < 4) return QStringLiteral("US");
    // The fourth letter of a game's ID is its region (GameTDB's folders
    // are by language, the way Dolphin and USB Loader GX pick them).
    switch (gameId.at(3).toUpper().unicode()) {
    case 'E': case 'N': return QStringLiteral("US");
    case 'J': return QStringLiteral("JA");
    case 'K': case 'Q': case 'T': return QStringLiteral("KO");
    case 'W': return QStringLiteral("ZH");
    case 'D': return QStringLiteral("DE");
    case 'F': return QStringLiteral("FR");
    case 'I': return QStringLiteral("IT");
    case 'S': return QStringLiteral("ES");
    case 'H': return QStringLiteral("NL");
    case 'U': return QStringLiteral("AU");
    default: return QStringLiteral("EN");
    }
}

QStringList CoverArt::candidates(const QString& gameId) {
    QStringList regions{regionFor(gameId)};
    for (const char* other : {"EN", "US", "JA"})
        if (!regions.contains(QLatin1String(other))) regions << QLatin1String(other);
    // A channel's covers are filed under its four letters on GameTDB, a
    // disc's under all six.
    QStringList ids{gameId};
    if (gameId.size() == 6) ids << gameId.left(4);
    QStringList out;
    for (const QString& id : ids)
        for (const char* kind : {"cover", "disc"})
            for (const QString& region : regions)
                out << QStringLiteral("https://art.gametdb.com/wii/%1/%2/%3.png").arg(QLatin1String(kind), region, id);
    return out;
}

void CoverArt::find(const QString& gameId) {
    if (known_.contains(gameId)) {
        emit found(gameId, known_.value(gameId));
        return;
    }
    if (busy_.contains(gameId)) return;
    busy_ << gameId;
    tryNext(gameId, candidates(gameId));
}

void CoverArt::tryNext(const QString& gameId, QStringList left) {
    if (left.isEmpty()) {
        busy_.removeAll(gameId);
        known_.insert(gameId, QString());
        emit found(gameId, QString());
        return;
    }
    const QString url = left.takeFirst();
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(8000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("RiftWii-RPC/" RIFTWII_RPC_VERSION));
    QNetworkReply* reply = network_.head(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gameId, url, left] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status == 200) {
            busy_.removeAll(gameId);
            known_.insert(gameId, url);
            emit found(gameId, url);
            return;
        }
        // Offline, or GameTDB not answering: not "no cover", so nothing is
        // kept and the next play asks again.
        if (status == 0 && reply->error() != QNetworkReply::ContentNotFoundError) {
            busy_.removeAll(gameId);
            emit found(gameId, QString());
            return;
        }
        tryNext(gameId, left);
    });
}

}  // namespace riftwii::rpc
