// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii::wii {

bool open_nand_permissions(const char* who);
void close_nand_permissions(const char* who);
// Before an IOS reload or game handoff: close every outstanding use and
// forget the address even if the next IOS has the same version/revision.
// Permission sessions must end before either boundary.
void CloseAllNandPermissions(const char* who);
void RecoverNandPermissions();
void LogNandPermissionRecovery();

// Open before the batch's first protected request. A lazy session lets ES
// try first; a failed opening is attempted only once for this batch.
class NandPermissionSession {
public:
    explicit NandPermissionSession(const char* who, bool start = true) : who_(who) {
        if (start) Open();
    }
    ~NandPermissionSession() {
        if (opened_) close_nand_permissions(who_);
    }
    NandPermissionSession(const NandPermissionSession&) = delete;
    NandPermissionSession& operator=(const NandPermissionSession&) = delete;

    bool Open() {
        if (!attempted_) {
            attempted_ = true;
            opened_ = open_nand_permissions(who_);
        }
        return opened_;
    }
    bool IsOpen() const { return opened_; }

private:
    const char* who_;
    bool attempted_ = false;
    bool opened_ = false;
};

}  // namespace riftwii::wii
