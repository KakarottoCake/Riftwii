// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.hpp"

#include <QJsonArray>

#include <algorithm>

namespace riftwii::rpc {
namespace {

// The Wii sends a game's message three times in case one is lost; copies
// within this many seconds are the same start.
constexpr qint64 kCopiesSeconds = 10;
// The menu says it is there every 30 s; it is taken as gone after it has
// missed a few (a busy Wi-Fi drops some).
constexpr qint64 kMenuMissed = 3;
constexpr qint64 kMenuMinimumSeconds = 90;
// The list of Wiis forgets one not heard from in a day.
constexpr qint64 kForgetWiiSeconds = 24 * 3600;
// More Wiis than anyone has on one network: anyone there can send made-up
// console tags, and the list must not grow with them.
constexpr int kMaxWiis = 32;

const char* const kRiftWiiUrl = "https://github.com/KakarottoCake/Riftwii";

// Discord takes 2 to 128 characters in its text fields and refuses the
// whole activity otherwise.
QString discordText(QString text) {
    text = text.simplified();
    if (text.size() > 128) text = text.left(127) + QChar(0x2026);
    while (text.size() < 2) text += QChar(' ');
    return text;
}

}  // namespace

Controller::Controller(QObject* parent) : QObject(parent) {}

void Controller::setSettings(const Settings& settings) {
    const bool menuNowHidden = settings_.showMenu && !settings.showMenu;
    settings_ = settings;
    if (state_ == State::Menu && menuNowHidden) {
        setIdle(QStringLiteral("the menu is no longer shown"));
        return;
    }
    // A new timeout counts from the game's start, as the old one did.
    if (state_ == State::Game) expires_ = start_ + qint64(settings_.timeoutMinutes) * 60;
    publish();
}

bool Controller::validGameId(const QString& id) {
    if (id.size() != 4 && id.size() != 6) return false;
    return std::all_of(id.begin(), id.end(), [](QChar c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    });
}

QString Controller::platformLabel(const QString& platform, const QString& system) {
    if (platform == "gamecube") return QStringLiteral("GameCube");
    if (platform == "wiiware") return QStringLiteral("WiiWare");
    if (platform == "vc")
        return system.isEmpty() ? QStringLiteral("Virtual Console")
                                : QStringLiteral("Virtual Console (%1)").arg(system.left(32));
    return QStringLiteral("Wii");
}

void Controller::remember(const PresenceMessage& message, const QHostAddress& from, qint64 now) {
    const QString console = QString::fromStdString(message.console);
    bool changed = false;
    auto it = std::find_if(wiis_.begin(), wiis_.end(), [&](const WiiSeen& w) { return w.console == console; });
    if (it == wiis_.end()) {
        if (wiis_.size() >= kMaxWiis) return;
        wiis_.push_back(WiiSeen{console, from, {}, now});
        it = wiis_.end() - 1;
        changed = true;
    }
    changed = changed || it->address != from;
    it->address = from;
    it->version = QString::fromStdString(message.version);
    it->lastHeard = now;
    const auto old = std::remove_if(wiis_.begin(), wiis_.end(),
                                    [&](const WiiSeen& w) { return now - w.lastHeard > kForgetWiiSeconds; });
    changed = changed || old != wiis_.end();
    wiis_.erase(old, wiis_.end());
    if (changed) emit wiisChanged();
}

void Controller::handle(const PresenceMessage& message, const QHostAddress& from, qint64 now) {
    remember(message, from, now);
    const QString console = QString::fromStdString(message.console);
    if (!settings_.console.isEmpty() && console != settings_.console) return;
    // With several Wiis, the one playing a game keeps the presence: another's
    // menu does not end it.
    if (state_ == State::Game && message.event != PresenceEvent::Game && console != console_) return;

    switch (message.event) {
    case PresenceEvent::Bye:
        if (state_ != State::Idle) setIdle(QStringLiteral("RiftWii closed"));
        return;

    case PresenceEvent::Menu: {
        const qint64 next = std::max<qint64>(message.next_seconds, kPresenceMenuSeconds);
        if (!settings_.showMenu) {
            if (state_ == State::Game) setIdle(QStringLiteral("RiftWii is back in its menu"));
            return;
        }
        if (state_ != State::Menu) {
            if (state_ == State::Game) emit logLine(QStringLiteral("RiftWii is back in its menu: the game has ended."));
            else emit logLine(QStringLiteral("RiftWii is in its menu (%1).").arg(from.toString()));
            start_ = now;
        }
        state_ = State::Menu;
        console_ = console;
        version_ = QString::fromStdString(message.version);
        expires_ = now + std::max(next * kMenuMissed, kMenuMinimumSeconds);
        publish();
        return;
    }

    case PresenceEvent::Game: {
        const QString id = QString::fromStdString(message.game_id).toUpper();
        if (!validGameId(id)) {
            emit logLine(QStringLiteral("A game with an ID that is not one (%1) was left out.").arg(id.left(16)));
            return;
        }
        const bool same = state_ == State::Game && id == gameId_ && console == console_;
        if (same && now - lastGameMessage_ < kCopiesSeconds) return;  // a copy of the same message
        lastGameMessage_ = now;
        // The game's own Reset goes through RiftWii and says so: the same
        // play goes on, with its time.
        const bool keepTime = same && message.restart;
        state_ = State::Game;
        console_ = console;
        version_ = QString::fromStdString(message.version);
        if (gameId_ != id) cover_.clear();
        gameId_ = id;
        title_ = QString::fromStdString(message.title).simplified();
        if (title_.isEmpty()) title_ = id;
        platform_ = QString::fromStdString(message.platform);
        system_ = QString::fromStdString(message.system);
        mods_.clear();
        for (const std::string& mod : message.mods) mods_ << QString::fromStdString(mod).simplified();
        if (!keepTime) start_ = now;
        expires_ = start_ + qint64(settings_.timeoutMinutes) * 60;
        emit logLine(QStringLiteral("Playing %1 (%2)%3.")
                         .arg(title_, id, mods_.isEmpty() ? QString() : QStringLiteral(" with ") + mods_.join(", ")));
        publish();
        if (cover_.isEmpty()) emit coverWanted(id);
        return;
    }
    }
}

void Controller::tick(qint64 now) {
    if (state_ == State::Idle || now < expires_) return;
    if (state_ == State::Game)
        setIdle(QStringLiteral("nothing heard from the Wii for %1").arg(timeoutLabel(settings_.timeoutMinutes)));
    else
        setIdle(QStringLiteral("the RiftWii menu stopped saying it is there"));
}

void Controller::clear() {
    if (state_ != State::Idle) setIdle(QStringLiteral("cleared by hand"));
}

void Controller::setCover(const QString& gameId, const QString& url) {
    if (gameId != gameId_ || url == cover_) return;
    cover_ = url;
    if (state_ == State::Game) publish();
}

void Controller::setIdle(const QString& why) {
    state_ = State::Idle;
    expires_ = 0;
    emit logLine(QStringLiteral("Presence cleared: %1.").arg(why));
    publish();
}

QJsonObject Controller::activity() const {
    if (state_ == State::Idle) return {};
    const QString icon = settings_.effectiveSmallImage();
    const QString riftwii = version_.isEmpty() ? QStringLiteral("RiftWii") : QStringLiteral("RiftWii ") + version_;
    QJsonObject assets;
    QJsonObject out;
    if (state_ == State::Game) {
        QString line = platformLabel(platform_, system_);
        if (settings_.showMods && !mods_.isEmpty()) line += QStringLiteral(" · ") + mods_.join(", ");
        out.insert("details", discordText(title_));
        out.insert("state", discordText(line));
        if (!cover_.isEmpty()) {
            assets.insert("large_image", cover_);
            assets.insert("large_text", discordText(title_));
            assets.insert("small_image", icon);
            assets.insert("small_text", discordText(riftwii));
        } else {
            assets.insert("large_image", icon);
            assets.insert("large_text", discordText(riftwii));
        }
    } else {
        out.insert("details", discordText(QStringLiteral("In the RiftWii menu")));
        out.insert("state", discordText(QStringLiteral("Choosing a game")));
        assets.insert("large_image", icon);
        assets.insert("large_text", discordText(riftwii));
    }
    out.insert("assets", assets);
    out.insert("timestamps", QJsonObject{{"start", start_}});
    if (settings_.showButton)
        out.insert("buttons", QJsonArray{QJsonObject{{"label", "Get RiftWii"}, {"url", kRiftWiiUrl}}});
    return out;
}

QString Controller::summary() const {
    switch (state_) {
    case State::Game: return QStringLiteral("Playing %1").arg(title_);
    case State::Menu: return QStringLiteral("In the RiftWii menu");
    case State::Idle: break;
    }
    return QStringLiteral("Nothing shown");
}

void Controller::publish() { emit activityChanged(activity()); }

}  // namespace riftwii::rpc
