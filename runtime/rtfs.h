/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ISFS request adapter over rtfat.  The hook layer gives this module an
 * IOS request block before it submits it; rtfs either leaves that request
 * alone, completes it locally, or drives its embedded rtfat operation.
 * It contains no IOS, SD, callback, allocation, or libc dependency.
 */
#ifndef RIFTWII_RTFS_H
#define RIFTWII_RTFS_H

#include <stdint.h>

#include "rtfat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTFS_PATH_BYTES 64u       /* IPC_MAXPATH_LEN / ISFS_MAXPATH */
#define RTFS_MAX_FDS 8u
#define RTFS_FD_BASE 0x40000000u  /* fake descriptors occupy this high range */

/* Synthetic metadata for every redirected file and the redirected directory.
 * ISFS permission bits are read (1) and write (2), hence 3 means read/write. */
#define RTFS_META_OWNER_ID 0u
#define RTFS_META_GROUP_ID 0u
#define RTFS_META_ATTRIBUTES 0u
#define RTFS_META_OWNER_PERM 3u
#define RTFS_META_GROUP_PERM 3u
#define RTFS_META_OTHER_PERM 3u

/* IOS request commands.  rtfs_ipc is the 0x28-byte active portion of the
 * 0x40-byte IOS IPC message; callback/user_data follow the five arg words. */
#define RTFS_CMD_OPEN 1u
#define RTFS_CMD_CLOSE 2u
#define RTFS_CMD_READ 3u
#define RTFS_CMD_WRITE 4u
#define RTFS_CMD_SEEK 5u
#define RTFS_CMD_IOCTL 6u
#define RTFS_CMD_IOCTLV 7u

#define RTFS_SEEK_SET 0u
#define RTFS_SEEK_CUR 1u
#define RTFS_SEEK_END 2u

#define RTFS_IOCTL_CREATEDIR 0x03u
#define RTFS_IOCTL_READDIR 0x04u
#define RTFS_IOCTL_SETATTR 0x05u
#define RTFS_IOCTL_GETATTR 0x06u
#define RTFS_IOCTL_DELETE 0x07u
#define RTFS_IOCTL_RENAME 0x08u
#define RTFS_IOCTL_CREATEFILE 0x09u
#define RTFS_IOCTL_GETFILESTATS 0x0Bu
#define RTFS_IOCTL_GETUSAGE 0x0Cu

/* Request classifications. */
#define RTFS_PASS_THROUGH 0u
#define RTFS_COMPLETE 1u
#define RTFS_NEEDS_IO 2u

/* These match libogc's isfs_cb payloads. */
struct rtfs_attr_block {
    uint32_t owner_id;
    uint16_t group_id;
    char filepath[RTFS_PATH_BYTES];
    uint8_t ownerperm;
    uint8_t groupperm;
    uint8_t otherperm;
    uint8_t attributes;
    uint8_t pad[2];
};

struct rtfs_iovec {
    uint32_t data;
    uint32_t len;
};

struct rtfs_ipc {
    uint32_t command;
    int32_t result;
    int32_t fd;
    union {
        struct { uint32_t path; uint32_t mode; } open;
        struct { uint32_t data; uint32_t length; } readwrite;
        struct { int32_t where; uint32_t whence; } seek;
        struct { uint32_t request; uint32_t in; uint32_t in_len; uint32_t out; uint32_t out_len; } ioctl;
        struct { uint32_t request; uint32_t in_count; uint32_t out_count; uint32_t vectors; } ioctlv;
    } args;
    uint32_t callback;
    uint32_t user_data;
};

struct rtfs_file {
    uint32_t in_use;
    uint32_t generation;
    uint32_t mode;
    char name[RTFAT_NAME_MAX + 1];
    struct rtfat_file fat;
};

struct rtfs_context {
    struct rtfat_volume volume;
    int32_t fs_fd;              /* the game's /dev/fs fd */
    uint32_t prefix_len;
    uint32_t busy;
    char data_prefix[RTFS_PATH_BYTES];
    struct rtfs_file files[RTFS_MAX_FDS];
};

/* Internal request actions are intentionally visible for host diagnostics;
 * callers only need classification, result, callback, user_data, and fat. */
#define RTFS_ACTION_NONE 0u
#define RTFS_ACTION_OPEN 1u
#define RTFS_ACTION_READ 2u
#define RTFS_ACTION_WRITE 3u
#define RTFS_ACTION_CREATE 4u
#define RTFS_ACTION_DELETE 5u
#define RTFS_ACTION_RENAME 6u
#define RTFS_ACTION_ATTR 7u
#define RTFS_ACTION_SETATTR 8u
#define RTFS_ACTION_LIST 9u
#define RTFS_ACTION_USAGE 10u

struct rtfs_request {
    uint32_t classification;
    int32_t result;
    uint32_t callback;
    uint32_t user_data;
    uint32_t active;
    uint32_t action;
    uint32_t slot;
    uint32_t out0;
    uint32_t out1;
    struct rtfat_op fat;
};

/* Copies the loader supplied directory prefix and volume.  Returns zero on
 * success, RTFAT_EINVAL for an unterminated/invalid prefix. */
int rtfs_init(struct rtfs_context* ctx, const struct rtfat_volume* volume, const char* data_prefix, int32_t fs_fd);

/* Starts one IOS request.  The caller sets request.fat.bounce and
 * request.fat.bounce_bytes before this call for Read/Write.  A complete
 * request's result is ready immediately.  For RTFS_NEEDS_IO call rtfs_step
 * until it returns RTFAT_DONE, performing each requested transfer and
 * putting its status in request.fat.io_status before resuming. */
void rtfs_begin(struct rtfs_context* ctx, struct rtfs_request* request, const struct rtfs_ipc* ipc);
int rtfs_step(struct rtfs_context* ctx, struct rtfs_request* request);

#ifdef __cplusplus
}
#endif

#endif
