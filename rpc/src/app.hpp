// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QTimer>

#include "controller.hpp"
#include "coverart.hpp"
#include "discordipc.hpp"
#include "listener.hpp"
#include "settings.hpp"

namespace riftwii::rpc {

// The pieces wired together: the Wii's messages go to the controller, the
// controller's activity to Discord, covers looked up on the way. The same
// with a window, in the tray, or with no screen at all (--headless).
class App : public QObject {
    Q_OBJECT
public:
    explicit App(QObject* parent = nullptr);
    // False with `error` when the Wii's port cannot be listened on.
    bool start(quint16 port, QString& error);

    const Settings& settings() const { return settings_; }
    void applySettings(const Settings& settings);
    // --client-id: used instead of the saved one for this run, never saved.
    void setRunClientId(const QString& id) { runClientId_ = id.trimmed(); }
    QString clientId() const;

    Controller& controller() { return controller_; }
    DiscordIpc& discord() { return discord_; }
    quint16 port() const { return listener_.port(); }

signals:
    void logLine(const QString& text);

private:
    Settings settings_;
    QString runClientId_;
    Listener listener_;
    Controller controller_;
    DiscordIpc discord_;
    CoverArt covers_;
    QTimer clock_;
};

}  // namespace riftwii::rpc
