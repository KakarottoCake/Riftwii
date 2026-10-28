// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMainWindow>
#include <QSystemTrayIcon>

#include "settings.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace riftwii::rpc {

class App;

// The settings and what is happening, and the tray icon that stays when
// the window is closed. The work is App's: closing the window changes
// nothing about the presence.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(App& app);
    // Shown from the tray, or by a second copy started while this one runs.
    void bringToFront();
    bool hasTray() const { return tray_ != nullptr; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildTray();
    void loadSettings();
    void saveSettings();
    void refreshStatus();
    void refreshWiis();
    void appendLog(const QString& line);

    App& app_;
    QSystemTrayIcon* tray_ = nullptr;
    QAction* trayStatus_ = nullptr;
    bool trayHintShown_ = false;
    bool loading_ = false;

    QLabel* discordStatus_ = nullptr;
    QLabel* wiiStatus_ = nullptr;
    QLabel* presenceStatus_ = nullptr;
    QLineEdit* clientId_ = nullptr;
    QComboBox* timeout_ = nullptr;
    QComboBox* wii_ = nullptr;
    QCheckBox* showMenu_ = nullptr;
    QCheckBox* showMods_ = nullptr;
    QCheckBox* showButton_ = nullptr;
    QCheckBox* startHidden_ = nullptr;
    QCheckBox* autostart_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
};

}  // namespace riftwii::rpc
