// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.hpp"

#include <QSettings>

namespace riftwii::rpc {
namespace {

// The icon in RiftWii's repository, which Discord fetches itself: no
// picture has to be uploaded to the Discord application for it.
const char* const kIconUrl =
    "https://raw.githubusercontent.com/KakarottoCake/Riftwii/main/rpc/icons/riftwii-rpc.png";

int clampTimeout(int minutes) {
    const QVector<int> choices = timeoutChoices();
    if (minutes <= choices.front()) return choices.front();
    if (minutes >= choices.back()) return choices.back();
    return minutes / 10 * 10;
}

}  // namespace

QVector<int> timeoutChoices() {
    QVector<int> out;
    for (int minutes = 10; minutes <= 12 * 60; minutes += 10) out.push_back(minutes);
    return out;
}

QString timeoutLabel(int minutes) {
    const int hours = minutes / 60;
    const int rest = minutes % 60;
    const QString hourText = hours == 1 ? QStringLiteral("1 hour") : QStringLiteral("%1 hours").arg(hours);
    if (hours == 0) return QStringLiteral("%1 minutes").arg(rest);
    if (rest == 0) return hourText;
    return QStringLiteral("%1 %2 minutes").arg(hourText).arg(rest);
}

QString Settings::effectiveClientId() const {
    const QString trimmed = clientId.trimmed();
    return trimmed.isEmpty() ? QStringLiteral(RIFTWII_RPC_CLIENT_ID) : trimmed;
}

QString Settings::effectiveSmallImage() const {
    const QString trimmed = smallImage.trimmed();
    return trimmed.isEmpty() ? QString::fromLatin1(kIconUrl) : trimmed;
}

Settings Settings::load() {
    QSettings store;
    Settings s;
    s.clientId = store.value("discord/clientId").toString();
    s.smallImage = store.value("discord/smallImage").toString();
    s.timeoutMinutes = clampTimeout(store.value("presence/timeoutMinutes", s.timeoutMinutes).toInt());
    s.showMenu = store.value("presence/showMenu", s.showMenu).toBool();
    s.showMods = store.value("presence/showMods", s.showMods).toBool();
    s.showButton = store.value("presence/showButton", s.showButton).toBool();
    s.console = store.value("wii/console").toString();
    s.startHidden = store.value("app/startHidden", s.startHidden).toBool();
    return s;
}

void Settings::save() const {
    QSettings store;
    store.setValue("discord/clientId", clientId.trimmed());
    store.setValue("discord/smallImage", smallImage.trimmed());
    store.setValue("presence/timeoutMinutes", clampTimeout(timeoutMinutes));
    store.setValue("presence/showMenu", showMenu);
    store.setValue("presence/showMods", showMods);
    store.setValue("presence/showButton", showButton);
    store.setValue("wii/console", console);
    store.setValue("app/startHidden", startHidden);
}

}  // namespace riftwii::rpc
