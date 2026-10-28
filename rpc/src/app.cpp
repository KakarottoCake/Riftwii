// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app.hpp"

#include <QDateTime>

namespace riftwii::rpc {
namespace {

qint64 now() { return QDateTime::currentSecsSinceEpoch(); }

}  // namespace

App::App(QObject* parent) : QObject(parent) {
    qRegisterMetaType<riftwii::PresenceMessage>();
    settings_ = Settings::load();
    controller_.setSettings(settings_);

    connect(&listener_, &Listener::received, this,
            [this](const PresenceMessage& message, const QHostAddress& from) { controller_.handle(message, from, now()); });
    connect(&controller_, &Controller::activityChanged, &discord_, &DiscordIpc::setActivity);
    connect(&controller_, &Controller::coverWanted, &covers_, &CoverArt::find);
    connect(&covers_, &CoverArt::found, &controller_, &Controller::setCover);
    connect(&controller_, &Controller::logLine, this, &App::logLine);
    connect(&discord_, &DiscordIpc::problem, this, &App::logLine);
    connect(&discord_, &DiscordIpc::readyChanged, this, [this](bool ready) {
        if (ready) emit logLine(QStringLiteral("Connected to Discord as %1.").arg(discord_.userName()));
    });
    // Every few seconds: a presence whose time is up ends even when no
    // message comes to end it.
    connect(&clock_, &QTimer::timeout, this, [this] { controller_.tick(now()); });
    clock_.start(5000);
}

bool App::start(quint16 port, QString& error) {
    if (!listener_.start(port, error)) return false;
    emit logLine(QStringLiteral("Listening for RiftWii on UDP port %1.").arg(listener_.port()));
    const QString id = clientId();
    if (id.isEmpty())
        emit logLine(QStringLiteral("No Discord application ID yet: paste one in the window (see the README)."));
    discord_.start(id);
    return true;
}

void App::applySettings(const Settings& settings) {
    settings_ = settings;
    settings_.save();
    controller_.setSettings(settings_);
    discord_.start(clientId());
}

QString App::clientId() const { return runClientId_.isEmpty() ? settings_.effectiveClientId() : runClientId_; }

}  // namespace riftwii::rpc
