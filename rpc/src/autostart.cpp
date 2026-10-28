// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "autostart.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace riftwii::rpc {
namespace {

#if defined(Q_OS_WIN)

const char* const kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

QString command() {
    return QStringLiteral("\"%1\" --background").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}

#else

QString entryPath() {
#if defined(Q_OS_MACOS)
    return QDir::homePath() + "/Library/LaunchAgents/xyz.riftwii.rpc.plist";
#else
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/riftwii-rpc.desktop";
#endif
}

QString xmlEscaped(QString text) {
    return text.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
}

QString entryText() {
    const QString program = QCoreApplication::applicationFilePath();
#if defined(Q_OS_MACOS)
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
               "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
               "<plist version=\"1.0\">\n<dict>\n"
               "  <key>Label</key><string>xyz.riftwii.rpc</string>\n"
               "  <key>ProgramArguments</key>\n"
               "  <array><string>%1</string><string>--background</string></array>\n"
               "  <key>RunAtLoad</key><true/>\n"
               "  <key>ProcessType</key><string>Interactive</string>\n"
               "</dict>\n</plist>\n")
        .arg(xmlEscaped(program));
#else
    // The Exec key's own quoting: the path in double quotes, with ", `, $
    // and \ escaped.
    QString quoted = program;
    quoted.replace('\\', "\\\\").replace('"', "\\\"").replace('`', "\\`").replace('$', "\\$");
    return QStringLiteral(
               "[Desktop Entry]\nType=Application\nName=RiftWii-RPC\n"
               "Comment=Discord rich presence for RiftWii\nExec=\"%1\" --background\n"
               "Icon=riftwii-rpc\nX-GNOME-Autostart-enabled=true\n")
        .arg(quoted);
#endif
}

#endif

}  // namespace

bool autostartEnabled() {
#if defined(Q_OS_WIN)
    return QSettings(kRunKey, QSettings::NativeFormat).contains("RiftWii-RPC");
#else
    return QFile::exists(entryPath());
#endif
}

bool setAutostart(bool on, QString& error) {
#if defined(Q_OS_WIN)
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (on) run.setValue("RiftWii-RPC", command());
    else run.remove("RiftWii-RPC");
    run.sync();
    if (run.status() != QSettings::NoError) {
        error = QStringLiteral("Windows did not let RiftWii-RPC change its start-up entry.");
        return false;
    }
    return true;
#else
    const QString path = entryPath();
    if (!on) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            error = QStringLiteral("Could not remove %1.").arg(path);
            return false;
        }
        return true;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(entryText().toUtf8()) < 0 || !file.commit()) {
        error = QStringLiteral("Could not write %1.").arg(path);
        return false;
    }
    return true;
#endif
}

}  // namespace riftwii::rpc
