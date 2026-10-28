// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>

namespace riftwii::rpc {

// The presence timeouts offered, in minutes: RiiTag-RPC's own choices
// (ten minutes to twelve hours, in steps of ten), so the two feel alike.
QVector<int> timeoutChoices();
// "10 minutes", "1 hour", "2 hours 30 minutes".
QString timeoutLabel(int minutes);

// What the player chose, kept by QSettings (the registry on Windows, a
// plist on macOS, an .ini file elsewhere).
struct Settings {
    // Discord's application ID. Empty: the one built in
    // (RIFTWII_RPC_CLIENT_ID), if any.
    QString clientId;
    // How long a game stays on Discord with nothing heard from the Wii.
    // A game sends nothing while it runs (the Wii's network is the game's
    // then), so this is how long a play is shown at most unless RiftWii
    // comes back or closes before.
    int timeoutMinutes = 120;
    bool showMenu = true;     // "In the RiftWii menu" while RiftWii's menu is open
    bool showMods = true;     // the packs turned on, under the game's name
    bool showButton = true;   // a "Get RiftWii" button on the presence
    // Only this Wii (its console tag); empty for any on the network.
    QString console;
    bool startHidden = false;  // start in the tray, without the window
    // The small picture: an asset key in the Discord application, or an
    // https link. Empty for RiftWii-RPC's icon from the repository.
    QString smallImage;

    QString effectiveClientId() const;
    QString effectiveSmallImage() const;

    static Settings load();
    void save() const;
};

}  // namespace riftwii::rpc
