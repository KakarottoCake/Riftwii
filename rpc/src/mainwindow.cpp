// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "app.hpp"
#include "autostart.hpp"

namespace riftwii::rpc {
namespace {

QString since(qint64 seconds) {
    return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("HH:mm"));
}

}  // namespace

MainWindow::MainWindow(App& app) : app_(app) {
    setWindowTitle(QStringLiteral("RiftWii-RPC"));
    setWindowIcon(QIcon(QStringLiteral(":/riftwii-rpc.png")));

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* status = new QGroupBox(QStringLiteral("Now"), central);
    auto* statusForm = new QFormLayout(status);
    discordStatus_ = new QLabel(status);
    wiiStatus_ = new QLabel(status);
    presenceStatus_ = new QLabel(status);
    presenceStatus_->setWordWrap(true);
    statusForm->addRow(QStringLiteral("Discord:"), discordStatus_);
    statusForm->addRow(QStringLiteral("Wii:"), wiiStatus_);
    statusForm->addRow(QStringLiteral("Presence:"), presenceStatus_);
    auto* clear = new QPushButton(QStringLiteral("Clear the presence now"), status);
    statusForm->addRow(QString(), clear);
    layout->addWidget(status);

    auto* options = new QGroupBox(QStringLiteral("Settings"), central);
    auto* form = new QFormLayout(options);
    clientId_ = new QLineEdit(options);
    clientId_->setPlaceholderText(QStringLiteral(RIFTWII_RPC_CLIENT_ID).isEmpty()
                                      ? QStringLiteral("From discord.com/developers/applications")
                                      : QStringLiteral("Built in: %1").arg(RIFTWII_RPC_CLIENT_ID));
    form->addRow(QStringLiteral("Discord application ID:"), clientId_);
    timeout_ = new QComboBox(options);
    for (int minutes : timeoutChoices()) timeout_->addItem(timeoutLabel(minutes), minutes);
    timeout_->setToolTip(QStringLiteral(
        "A Wii says nothing while a game runs. The game stays on Discord until RiftWii is back, "
        "RiftWii closes, or this much time has passed."));
    form->addRow(QStringLiteral("Keep a game shown for:"), timeout_);
    wii_ = new QComboBox(options);
    form->addRow(QStringLiteral("Wii:"), wii_);
    showMenu_ = new QCheckBox(QStringLiteral("Show when RiftWii's menu is open"), options);
    showMods_ = new QCheckBox(QStringLiteral("Show the mods turned on"), options);
    showButton_ = new QCheckBox(QStringLiteral("Show a \"Get RiftWii\" button"), options);
    startHidden_ = new QCheckBox(QStringLiteral("Start in the background (tray only)"), options);
    autostart_ = new QCheckBox(QStringLiteral("Start when I log in"), options);
    for (QCheckBox* box : {showMenu_, showMods_, showButton_, startHidden_, autostart_}) form->addRow(QString(), box);
    layout->addWidget(options);

    log_ = new QPlainTextEdit(central);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(500);
    log_->setMinimumHeight(120);
    layout->addWidget(log_, 1);

    auto* buttons = new QHBoxLayout;
    auto* hide = new QPushButton(QStringLiteral("Hide"), central);
    auto* quit = new QPushButton(QStringLiteral("Quit"), central);
    buttons->addStretch(1);
    buttons->addWidget(hide);
    buttons->addWidget(quit);
    layout->addLayout(buttons);
    setCentralWidget(central);
    resize(520, 640);

    loadSettings();
    buildTray();
    hide->setEnabled(hasTray());

    connect(clear, &QPushButton::clicked, this, [this] { app_.controller().clear(); });
    connect(hide, &QPushButton::clicked, this, &QWidget::hide);
    connect(quit, &QPushButton::clicked, qApp, &QApplication::quit);
    connect(clientId_, &QLineEdit::editingFinished, this, &MainWindow::saveSettings);
    connect(timeout_, &QComboBox::currentIndexChanged, this, &MainWindow::saveSettings);
    connect(wii_, &QComboBox::currentIndexChanged, this, &MainWindow::saveSettings);
    for (QCheckBox* box : {showMenu_, showMods_, showButton_, startHidden_})
        connect(box, &QCheckBox::toggled, this, &MainWindow::saveSettings);
    connect(autostart_, &QCheckBox::toggled, this, [this](bool on) {
        QString error;
        if (setAutostart(on, error)) return;
        QMessageBox::warning(this, QStringLiteral("RiftWii-RPC"), error);
        const QSignalBlocker block(autostart_);
        autostart_->setChecked(autostartEnabled());
    });

    connect(&app_, &App::logLine, this, &MainWindow::appendLog);
    connect(&app_.controller(), &Controller::activityChanged, this, &MainWindow::refreshStatus);
    connect(&app_.controller(), &Controller::wiisChanged, this, &MainWindow::refreshWiis);
    connect(&app_.discord(), &DiscordIpc::readyChanged, this, &MainWindow::refreshStatus);
    // "Heard 12 s ago" moves on by itself.
    auto* clock = new QTimer(this);
    connect(clock, &QTimer::timeout, this, &MainWindow::refreshStatus);
    clock->start(5000);
    refreshWiis();
    refreshStatus();
}

void MainWindow::buildTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    tray_ = new QSystemTrayIcon(QIcon(QStringLiteral(":/riftwii-rpc.png")), this);
    auto* menu = new QMenu(this);
    trayStatus_ = menu->addAction(QString());
    trayStatus_->setEnabled(false);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Show RiftWii-RPC"), this, &MainWindow::bringToFront);
    menu->addAction(QStringLiteral("Clear the presence"), this, [this] { app_.controller().clear(); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Quit"), qApp, &QApplication::quit);
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) bringToFront();
    });
    tray_->show();
}

void MainWindow::bringToFront() {
    show();
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // With a tray, closing the window keeps the presence going; without
    // one there would be no way back, so it quits.
    if (!hasTray()) {
        event->accept();
        qApp->quit();
        return;
    }
    hide();
    event->ignore();
    if (!trayHintShown_) {
        trayHintShown_ = true;
        tray_->showMessage(QStringLiteral("RiftWii-RPC"),
                           QStringLiteral("Still running in the background. Quit from this icon's menu."),
                           QSystemTrayIcon::Information, 4000);
    }
}

void MainWindow::loadSettings() {
    loading_ = true;
    const Settings& s = app_.settings();
    clientId_->setText(s.clientId);
    timeout_->setCurrentIndex(std::max(0, timeout_->findData(s.timeoutMinutes)));
    showMenu_->setChecked(s.showMenu);
    showMods_->setChecked(s.showMods);
    showButton_->setChecked(s.showButton);
    startHidden_->setChecked(s.startHidden);
    autostart_->setChecked(autostartEnabled());
    loading_ = false;
}

void MainWindow::saveSettings() {
    if (loading_) return;
    Settings s = app_.settings();
    s.clientId = clientId_->text();
    s.timeoutMinutes = timeout_->currentData().toInt();
    s.console = wii_->currentData().toString();
    s.showMenu = showMenu_->isChecked();
    s.showMods = showMods_->isChecked();
    s.showButton = showButton_->isChecked();
    s.startHidden = startHidden_->isChecked();
    app_.applySettings(s);
    refreshStatus();
}

void MainWindow::refreshWiis() {
    loading_ = true;
    const QString chosen = app_.settings().console;
    wii_->clear();
    wii_->addItem(QStringLiteral("Any Wii on this network"), QString());
    bool chosenListed = chosen.isEmpty();
    for (const WiiSeen& w : app_.controller().wiis()) {
        const QString name = w.console.isEmpty() ? QStringLiteral("unnamed") : w.console;
        wii_->addItem(QStringLiteral("%1 (%2)").arg(name, w.address.toString()), w.console);
        chosenListed = chosenListed || w.console == chosen;
    }
    // A Wii chosen earlier stays chosen while it is switched off.
    if (!chosenListed) wii_->addItem(QStringLiteral("%1 (not heard from yet)").arg(chosen), chosen);
    wii_->setCurrentIndex(std::max(0, wii_->findData(chosen)));
    loading_ = false;
}

void MainWindow::refreshStatus() {
    DiscordIpc& discord = app_.discord();
    if (app_.clientId().isEmpty())
        discordStatus_->setText(QStringLiteral("Needs a Discord application ID (below)"));
    else if (discord.isReady())
        discordStatus_->setText(QStringLiteral("Connected as %1").arg(discord.userName()));
    else
        discordStatus_->setText(QStringLiteral("Waiting for the Discord app to be open"));

    const QVector<WiiSeen> wiis = app_.controller().wiis();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (wiis.isEmpty()) {
        wiiStatus_->setText(QStringLiteral("Not heard from yet (UDP port %1)").arg(app_.port()));
    } else {
        const WiiSeen* last = &wiis.front();
        for (const WiiSeen& w : wiis)
            if (w.lastHeard > last->lastHeard) last = &w;
        wiiStatus_->setText(QStringLiteral("%1, RiftWii %2, heard %3 s ago")
                                .arg(last->address.toString(), last->version)
                                .arg(now - last->lastHeard));
    }

    const Controller& c = app_.controller();
    QString text = c.summary();
    if (c.state() == Controller::State::Game)
        text += QStringLiteral(" since %1, until %2 at the latest").arg(since(c.startedAt()), since(c.expiresAt()));
    presenceStatus_->setText(text);
    if (trayStatus_) trayStatus_->setText(text);
    if (tray_) tray_->setToolTip(QStringLiteral("RiftWii-RPC: ") + text);
}

void MainWindow::appendLog(const QString& line) {
    log_->appendPlainText(QStringLiteral("%1  %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), line));
}

}  // namespace riftwii::rpc
