// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVector>

#include "riftwii/presence.hpp"
#include "settings.hpp"

namespace riftwii::rpc {

// A Wii heard from, for the window's "Only this Wii" list.
struct WiiSeen {
    QString console;
    QHostAddress address;
    QString version;
    qint64 lastHeard = 0;  // seconds since 1970
};

// What Discord should show, from what the Wii says and how long ago:
//
//   game   the game, from its start, until RiftWii is heard from again
//          (the menu: the game has ended; a new game; closing) or the
//          timeout passes. A game sends nothing while it runs, so the
//          timeout is all there is when the Wii is switched off from a game.
//   menu   "In the RiftWii menu" while the menu keeps saying it is there
//          (Settings: off clears the presence instead).
//   bye    nothing at once.
//
// Times are passed in (seconds since 1970), so the tests can move them.
class Controller : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Menu, Game };

    explicit Controller(QObject* parent = nullptr);
    void setSettings(const Settings& settings);
    void handle(const PresenceMessage& message, const QHostAddress& from, qint64 now);
    // Called every few seconds: ends a presence whose time is up.
    void tick(qint64 now);
    // The player cleared it by hand.
    void clear();
    // The cover link for a game (CoverArt), empty for none.
    void setCover(const QString& gameId, const QString& url);

    State state() const { return state_; }
    QString gameId() const { return gameId_; }
    QString title() const { return title_; }
    qint64 startedAt() const { return start_; }
    qint64 expiresAt() const { return expires_; }
    QVector<WiiSeen> wiis() const { return wiis_; }
    // The activity as Discord takes it; empty for none.
    QJsonObject activity() const;
    // One line for the window and the tray.
    QString summary() const;

    // A game ID safe to put into a link: 4 or 6 letters and digits.
    static bool validGameId(const QString& id);
    // "Wii", "GameCube", "WiiWare", "Virtual Console (NES)".
    static QString platformLabel(const QString& platform, const QString& system);

signals:
    void activityChanged(const QJsonObject& activity);
    void wiisChanged();
    void coverWanted(const QString& gameId);
    void logLine(const QString& text);

private:
    void setIdle(const QString& why);
    void publish();
    void remember(const PresenceMessage& message, const QHostAddress& from, qint64 now);

    Settings settings_;
    State state_ = State::Idle;
    QString console_;  // the Wii the presence is for
    QString version_;
    QString gameId_;
    QString title_;
    QString platform_;
    QString system_;
    QStringList mods_;
    QString cover_;
    qint64 start_ = 0;
    qint64 expires_ = 0;
    qint64 lastGameMessage_ = 0;
    QVector<WiiSeen> wiis_;
};

}  // namespace riftwii::rpc
