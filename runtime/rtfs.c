/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rtfs.h"

#define RTFS_MODE_READ 0x01u
#define RTFS_MODE_WRITE 0x02u
enum { PATH_OUTSIDE = RTFS_PATH_OUTSIDE, PATH_DIR = RTFS_PATH_DIR, PATH_FILE = RTFS_PATH_FILE, PATH_BAD = RTFS_PATH_BAD };

static void zero_bytes(uint8_t* p, uint32_t n) { while (n--) *p++ = 0; }

static int fake_namespace(int32_t fd) { return fd >= (int32_t)RTFS_FD_BASE; }

static uint32_t bounded_length(const char* s, uint32_t limit) {
    uint32_t n;
    if (s == 0) return limit;
    for (n = 0; n < limit; ++n) if (s[n] == 0) return n;
    return limit;
}

static int same_bytes(const char* a, const char* b, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static void copy_name(char* out, const char* in) {
    uint32_t i;
    for (i = 0; i <= RTFAT_NAME_MAX; ++i) out[i] = in[i];
}

/* Identifies only paths whose prefix ends at a path component boundary.
 * A path underneath the prefix must be one rtfat name, never a directory.
 * Anything that is not a terminated path under the prefix is OUTSIDE:
 * a request is ours only once its path says so, and a malformed request
 * on another device (the same ioctl numbers serve the network devices)
 * goes to IOS, which refuses it itself. */
static int path_type(const struct rtfs_context* ctx, const char* path, char* name) {
    const uint32_t n = bounded_length(path, RTFS_PATH_BYTES);
    uint32_t i;
    if (ctx->prefix_len == 0) return PATH_OUTSIDE; /* no save directory served */
    if (n == RTFS_PATH_BYTES) return PATH_OUTSIDE;
    if (n < ctx->prefix_len || !same_bytes(path, ctx->data_prefix, ctx->prefix_len)) return PATH_OUTSIDE;
    if (n == ctx->prefix_len) return PATH_DIR;
    if (path[ctx->prefix_len] != '/') return PATH_OUTSIDE;
    if (n - ctx->prefix_len - 1u > RTFAT_NAME_MAX) return PATH_BAD;
    for (i = 0; i < n - ctx->prefix_len - 1u; ++i) {
        const char c = path[ctx->prefix_len + 1u + i];
        if (c == '/' || c == '\\') return PATH_BAD;
        name[i] = c;
    }
    name[i] = 0;
    return rtfat_valid_name(name) ? PATH_FILE : PATH_BAD;
}

static void finish(struct rtfs_context* ctx, struct rtfs_request* request, int32_t result) {
    request->classification = RTFS_COMPLETE;
    request->result = result;
    if (request->active) ctx->busy = 0;
    request->active = 0;
}

static void start_fat(struct rtfs_context* ctx, struct rtfs_request* request, uint32_t kind, uint32_t action) {
    request->action = action;
    request->classification = RTFS_NEEDS_IO;
    if (request->probe) return;
    request->active = 1;
    ctx->busy = 1;
    rtfat_begin(&request->fat, kind);
}

/* The engine's one-at-a-time rule, not applied to a probe. */
static int busy(const struct rtfs_context* ctx, const struct rtfs_request* request) {
    return ctx->busy && !request->probe;
}

/* ISFS requests on the fs device: any real fd until the game's /dev/fs
 * fd is known, then only that one. */
static int on_fs_device(const struct rtfs_context* ctx, int32_t fd) {
    return ctx->fs_fd < 0 ? !fake_namespace(fd) : fd == ctx->fs_fd;
}


static struct rtfs_file* fake_file(struct rtfs_context* ctx, int32_t fd, uint32_t* slot) {
    const uint32_t raw = (uint32_t)fd;
    const uint32_t v = raw - RTFS_FD_BASE;
    const uint32_t i = v & (RTFS_MAX_FDS - 1u);
    const uint32_t generation = v / RTFS_MAX_FDS;
    if (raw < RTFS_FD_BASE || i >= RTFS_MAX_FDS || generation == 0 || !ctx->files[i].in_use ||
        ctx->files[i].generation != generation) return 0;
    *slot = i;
    return &ctx->files[i];
}

static int32_t make_fd(const struct rtfs_file* file, uint32_t slot) {
    return (int32_t)(RTFS_FD_BASE + file->generation * RTFS_MAX_FDS + slot);
}

static struct rtfs_file* free_file(struct rtfs_context* ctx, uint32_t* slot) {
    uint32_t i;
    for (i = 0; i < RTFS_MAX_FDS; ++i) {
        if (!ctx->files[i].in_use && ctx->files[i].generation != 0) {
            *slot = i;
            return &ctx->files[i];
        }
    }
    return 0;
}

/* --- the "file" device (rtfs.h) --------------------------------------------- */

static int riivo_on(const struct rtfs_context* ctx) { return ctx->file_device != 0; }

/* "file" and whatever follows it: the device's name, spelled out. */
static int riivo_device_name(const char* p) {
    return p != 0 && p[0] == 'f' && p[1] == 'i' && p[2] == 'l' && p[3] == 'e';
}

static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void put_stats(uint32_t out, uint32_t id, uint32_t size, uint32_t is_dir) {
    uint8_t* s = (uint8_t*)(uintptr_t)out;
    put_be32(s, 0);
    put_be32(s + 4, id);
    put_be32(s + 8, 0);
    put_be32(s + 12, size);
    put_be32(s + 16, 0);
    put_be32(s + 20, is_dir ? RTFS_RIIVO_MODE_DIR : RTFS_RIIVO_MODE_FILE);
}

/* What a failed request answers: opens say "not opened". */
static int32_t riivo_failure(const struct rtfs_request* r) {
    return (r->final_action == RTFS_ACTION_R_OPEN || r->final_action == RTFS_ACTION_R_OPEN_CREATE ||
            r->final_action == RTFS_ACTION_R_OPENDIR) ? RTFS_RIIVO_NOT_OPENED : RTFS_RIIVO_ERROR;
}

/* Copies the game's path (at most `limit` bytes, NUL-terminated or not).
 * 0 when it does not fit. */
static int take_path(struct rtfs_request* r, const char* src, uint32_t limit) {
    uint32_t n;
    if (src == 0) return 0;
    if (limit > RTFS_RIIVO_PATH_BYTES) limit = RTFS_RIIVO_PATH_BYTES;
    for (n = 0; n < limit && src[n] != 0; ++n) r->path[n] = src[n];
    if (n == RTFS_RIIVO_PATH_BYTES) return 0;
    r->path[n] = 0;
    return 1;
}

/* The path's next name into fat.name: 1 (*last set when nothing follows
 * it), 0 when the path is used up, -1 for a name the card cannot hold
 * (".." included: nothing climbs out of a folder). "." and empty names
 * (doubled slashes) are skipped. */
static int next_component(struct rtfs_request* r, int* last) {
    uint32_t n, k;
    for (;;) {
        while (r->path[r->path_at] == '/') r->path_at++;
        if (r->path[r->path_at] == 0) return 0;
        n = 0;
        while (r->path[r->path_at] != 0 && r->path[r->path_at] != '/') {
            if (n >= RTFAT_NAME_MAX) return -1;
            r->fat.name[n++] = r->path[r->path_at++];
        }
        r->fat.name[n] = 0;
        if (!(n == 1 && r->fat.name[0] == '.')) break;
    }
    for (k = r->path_at; r->path[k] == '/'; ++k) {}
    *last = r->path[k] == 0;
    return rtfat_valid_name(r->fat.name) ? 1 : -1;
}

static struct rtfs_dir* dir_of(struct rtfs_context* ctx, int32_t handle, uint32_t* slot) {
    const uint32_t raw = (uint32_t)handle;
    uint32_t v, i;
    if (raw < RTFS_DIR_BASE || raw >= RTFS_FD_BASE) return 0;
    v = raw - RTFS_DIR_BASE;
    i = v % RTFS_MAX_DIRS;
    if (!ctx->dirs[i].in_use || ctx->dirs[i].generation != v / RTFS_MAX_DIRS) return 0;
    *slot = i;
    return &ctx->dirs[i];
}

static void riivo_opendir(struct rtfs_context* ctx, struct rtfs_request* r, uint32_t cluster) {
    uint32_t i;
    struct rtfs_dir* d;
    for (i = 0; i < RTFS_MAX_DIRS && ctx->dirs[i].in_use; ++i) {}
    if (i == RTFS_MAX_DIRS) { finish(ctx, r, RTFS_RIIVO_NOT_OPENED); return; }
    d = &ctx->dirs[i];
    if (!r->probe) {
        d->in_use = 1;
        d->cluster = cluster;
        d->cursor_cluster = 0;
        d->cursor_sector = 0;
        d->cursor_index = 0;
    }
    finish(ctx, r, (int32_t)(RTFS_DIR_BASE + d->generation * RTFS_MAX_DIRS + i));
}

/* The path named the folder the walk is in. */
static void riivo_at_folder(struct rtfs_context* ctx, struct rtfs_request* r) {
    if (r->final_action == RTFS_ACTION_R_OPENDIR) {
        riivo_opendir(ctx, r, r->walk_dir);
    } else if (r->final_action == RTFS_ACTION_R_STAT) {
        if (!r->probe) put_stats(r->out0, r->walk_dir, 0, 1);
        finish(ctx, r, RTFAT_OK);
    } else {
        finish(ctx, r, riivo_failure(r));
    }
}

/* Goes on from r->path_at in r->walk_dir: the next folder's lookup, the
 * final action on the last name, or an answer with no I/O. */
static void riivo_walk(struct rtfs_context* ctx, struct rtfs_request* r) {
    int last = 0;
    const int c = next_component(r, &last);
    if (c < 0) { finish(ctx, r, riivo_failure(r)); return; }
    if (c == 0) { riivo_at_folder(ctx, r); return; }
    r->fat.dir_cluster = r->walk_dir;
    r->fat.want_dirs = 1;
    if (!last) { start_fat(ctx, r, RTFAT_OP_LOOKUP, RTFS_ACTION_WALK); return; }
    switch (r->final_action) {
        case RTFS_ACTION_R_OPEN:
            r->fat.want_dirs = 0;
            start_fat(ctx, r, RTFAT_OP_LOOKUP, RTFS_ACTION_R_OPEN);
            return;
        case RTFS_ACTION_R_CREATEFILE: start_fat(ctx, r, RTFAT_OP_CREATE, RTFS_ACTION_R_CREATEFILE); return;
        case RTFS_ACTION_R_CREATEDIR: start_fat(ctx, r, RTFAT_OP_MKDIR, RTFS_ACTION_R_CREATEDIR); return;
        case RTFS_ACTION_R_DELETE: start_fat(ctx, r, RTFAT_OP_DELETE, RTFS_ACTION_R_DELETE); return;
        default: start_fat(ctx, r, RTFAT_OP_LOOKUP, r->final_action); return; /* OPENDIR, STAT */
    }
}

/* Starts a path request at the card's root. Riivolution's name for the
 * card's mount, "/mnt/sd", is the root too. */
static void riivo_start(struct rtfs_context* ctx, struct rtfs_request* r, uint32_t action) {
    const char* p = r->path;
    r->final_action = action;
    r->walk_dir = ctx->volume.root_cluster;
    r->path_at = 0;
    if (p[0] == '/' && p[1] == 'm' && p[2] == 'n' && p[3] == 't' && p[4] == '/' && p[5] == 's' && p[6] == 'd' &&
        (p[7] == 0 || p[7] == '/')) {
        r->path_at = 7;
    }
    if (busy(ctx, r)) { finish(ctx, r, RTFAT_EACCESS); return; }
    riivo_walk(ctx, r);
}

static void riivo_open(struct rtfs_context* ctx, struct rtfs_request* r, const char* path, uint32_t flags) {
    const uint32_t access = flags & RTFS_RIIVO_ACCMODE;
    r->final_action = RTFS_ACTION_R_OPEN;
    if (!take_path(r, path, RTFS_PATH_BYTES)) { finish(ctx, r, RTFS_RIIVO_NOT_OPENED); return; }
    r->flags = flags;
    r->fat.length = access == 0 ? RTFS_MODE_READ : access == 1 ? RTFS_MODE_WRITE : RTFS_MODE_READ | RTFS_MODE_WRITE;
    riivo_start(ctx, r, RTFS_ACTION_R_OPEN);
}

static void riivo_ioctl(struct rtfs_context* ctx, struct rtfs_request* r, const struct rtfs_ipc* ipc) {
    const uint32_t code = ipc->args.ioctl.request;
    const char* in = (const char*)(uintptr_t)ipc->args.ioctl.in;
    uint32_t action;
    if (code == RTFS_RIIVO_CLOSEDIR) {
        struct rtfs_dir* d;
        uint32_t slot;
        if (in == 0 || ipc->args.ioctl.in_len < 4u) { finish(ctx, r, RTFS_RIIVO_ERROR); return; }
        d = dir_of(ctx, *(const int32_t*)(const void*)in, &slot);
        if (d == 0) { finish(ctx, r, RTFS_RIIVO_ERROR); return; }
        if (!r->probe) {
            d->in_use = 0;
            if (++d->generation >= (RTFS_FD_BASE - RTFS_DIR_BASE) / RTFS_MAX_DIRS) d->generation = 1;
        }
        finish(ctx, r, RTFAT_OK);
        return;
    }
    switch (code) {
        case RTFS_RIIVO_STAT: action = RTFS_ACTION_R_STAT; break;
        case RTFS_RIIVO_CREATEFILE: action = RTFS_ACTION_R_CREATEFILE; break;
        case RTFS_RIIVO_DELETE: action = RTFS_ACTION_R_DELETE; break;
        case RTFS_RIIVO_CREATEDIR: action = RTFS_ACTION_R_CREATEDIR; break;
        case RTFS_RIIVO_OPENDIR: action = RTFS_ACTION_R_OPENDIR; break;
        default: finish(ctx, r, RTFS_RIIVO_ERROR); return; /* mounts, renames, logs: not served */
    }
    r->final_action = action;
    if (!take_path(r, in, ipc->args.ioctl.in_len)) { finish(ctx, r, riivo_failure(r)); return; }
    if (action == RTFS_ACTION_R_STAT) {
        if (ipc->args.ioctl.out == 0 || ipc->args.ioctl.out_len < RTFS_RIIVO_STATS_BYTES) {
            finish(ctx, r, RTFS_RIIVO_ERROR);
            return;
        }
        r->out0 = ipc->args.ioctl.out;
    }
    riivo_start(ctx, r, action);
}

static void riivo_ioctlv(struct rtfs_context* ctx, struct rtfs_request* r, const struct rtfs_ipc* ipc) {
    const struct rtfs_iovec* v = (const struct rtfs_iovec*)(uintptr_t)ipc->args.ioctlv.vectors;
    struct rtfs_dir* d;
    uint32_t slot;
    if (ipc->args.ioctlv.request != RTFS_RIIVO_NEXTDIR || ipc->args.ioctlv.in_count != 1u ||
        ipc->args.ioctlv.out_count != 2u || v == 0 || v[0].data == 0 || v[0].len < 4u || v[1].data == 0 ||
        v[1].len == 0 || v[2].data == 0 || v[2].len < RTFS_RIIVO_STATS_BYTES) {
        finish(ctx, r, RTFS_RIIVO_ERROR);
        return;
    }
    d = dir_of(ctx, *(const int32_t*)(uintptr_t)v[0].data, &slot);
    if (d == 0) { finish(ctx, r, RTFS_RIIVO_ERROR); return; }
    if (busy(ctx, r)) { finish(ctx, r, RTFAT_EACCESS); return; }
    r->final_action = RTFS_ACTION_R_NEXTDIR;
    r->slot = slot;
    r->out0 = v[1].data;
    r->out1 = v[2].data;
    r->fat.length = v[1].len;
    r->fat.dir_cluster = d->cluster;
    r->fat.cursor_cluster = d->cursor_cluster;
    r->fat.cursor_sector = d->cursor_sector;
    r->fat.cursor_index = d->cursor_index;
    start_fat(ctx, r, RTFAT_OP_NEXT, RTFS_ACTION_R_NEXTDIR);
}


static void put_attr(uint32_t out) {
    struct rtfs_attr_block* a = (struct rtfs_attr_block*)(uintptr_t)out;
    if (a == 0) return;
    a->owner_id = RTFS_META_OWNER_ID;
    a->group_id = RTFS_META_GROUP_ID;
    a->ownerperm = RTFS_META_OWNER_PERM;
    a->groupperm = RTFS_META_GROUP_PERM;
    a->otherperm = RTFS_META_OTHER_PERM;
    a->attributes = RTFS_META_ATTRIBUTES;
}

static int valid_attr_input(uint32_t in, uint32_t in_len, char* name, const struct rtfs_context* ctx) {
    const struct rtfs_attr_block* a = (const struct rtfs_attr_block*)(uintptr_t)in;
    if (a == 0 || in_len < (uint32_t)sizeof(*a)) return PATH_OUTSIDE;
    return path_type(ctx, a->filepath, name);
}

int rtfs_init(struct rtfs_context* ctx, const struct rtfat_volume* volume, const char* data_prefix, int32_t fs_fd) {
    uint32_t n, i;
    if (ctx == 0 || volume == 0) return RTFAT_EINVAL;
    n = data_prefix == 0 ? 0 : bounded_length(data_prefix, RTFS_PATH_BYTES);
    if (n == RTFS_PATH_BYTES || (n != 0 && (data_prefix[0] != '/' || data_prefix[n - 1u] == '/'))) return RTFAT_EINVAL;
    zero_bytes((uint8_t*)ctx, (uint32_t)sizeof(*ctx));
    ctx->volume = *volume;
    ctx->fs_fd = fs_fd;
    ctx->prefix_len = n;
    for (i = 0; i < n; ++i) ctx->data_prefix[i] = data_prefix[i];
    for (i = 0; i < RTFS_MAX_FDS; ++i) ctx->files[i].generation = 1;
    for (i = 0; i < RTFS_MAX_DIRS; ++i) ctx->dirs[i].generation = 1;
    return RTFAT_OK;
}

int rtfs_enable_file_device(struct rtfs_context* ctx) {
    const uint32_t root = ctx == 0 ? 0 : ctx->volume.root_cluster;
    if (ctx == 0 || root < 2u || root >= ctx->volume.cluster_count + 2u) return RTFAT_EINVAL;
    ctx->file_device = 1;
    return RTFAT_OK;
}

int rtfs_path_type(const struct rtfs_context* ctx, const char* path, char* name) {
    return ctx == 0 ? PATH_OUTSIDE : path_type(ctx, path, name);
}

int rtfs_is_fs_device(const char* path) {
    /* "/dev/fs", spelled out: the runtime keeps no string literals. */
    return path != 0 && path[0] == '/' && path[1] == 'd' && path[2] == 'e' && path[3] == 'v' && path[4] == '/' &&
           path[5] == 'f' && path[6] == 's' && path[7] == 0;
}

void rtfs_learn_fs_fd(struct rtfs_context* ctx, int32_t fd) {
    if (ctx != 0 && fd >= 0 && !fake_namespace(fd)) ctx->fs_fd = fd;
}

static void begin_open(struct rtfs_context* ctx, struct rtfs_request* r, const struct rtfs_ipc* ipc) {
    const char* path = (const char*)(uintptr_t)ipc->args.open.path;
    int p;
    if (riivo_on(ctx) && riivo_device_name(path)) {
        if (path[4] == 0) { finish(ctx, r, RTFS_RIIVO_FD); return; }
        if (path[4] == '/') { riivo_open(ctx, r, path + 4, ipc->args.open.mode); return; }
    }
    p = path_type(ctx, path, r->fat.name);
    if (p == PATH_OUTSIDE) return;
    if (p != PATH_FILE || (ipc->args.open.mode & ~(RTFS_MODE_READ | RTFS_MODE_WRITE)) != 0 || ipc->args.open.mode == 0) {
        finish(ctx, r, RTFAT_EINVAL); return;
    }
    if (busy(ctx, r)) { finish(ctx, r, RTFAT_EACCESS); return; }
    r->fat.length = ipc->args.open.mode;
    start_fat(ctx, r, RTFAT_OP_LOOKUP, RTFS_ACTION_OPEN);
}

static void begin_fd(struct rtfs_context* ctx, struct rtfs_request* r, const struct rtfs_ipc* ipc) {
    struct rtfs_file* f;
    uint32_t slot;
    if (ipc->fd == RTFS_RIIVO_FD && riivo_on(ctx)) {
        /* The device itself: closed, never read, written or sought. */
        finish(ctx, r, ipc->command == RTFS_CMD_CLOSE ? RTFAT_OK : RTFS_RIIVO_ERROR);
        return;
    }
    if (!fake_namespace(ipc->fd)) {
        /* The game closing its /dev/fs: the fd is free for reuse. */
        if (ipc->command == RTFS_CMD_CLOSE && ipc->fd == ctx->fs_fd && !r->probe) ctx->fs_fd = -1;
        return;
    }
    f = fake_file(ctx, ipc->fd, &slot);
    if (f == 0) { finish(ctx, r, RTFAT_EINVAL); return; }
    if (busy(ctx, r)) { finish(ctx, r, RTFAT_EACCESS); return; }
    r->slot = slot;
    if (ipc->command == RTFS_CMD_CLOSE) {
        if (!r->probe) {
            f->in_use = 0;
            if (++f->generation == 0 || f->generation >= (0x80000000u - RTFS_FD_BASE) / RTFS_MAX_FDS) f->generation = 0;
        }
        finish(ctx, r, RTFAT_OK);
    } else if (ipc->command == RTFS_CMD_SEEK) {
        int64_t base;
        int64_t target;
        const int32_t where = ipc->args.seek.where;
        if (ipc->args.seek.whence == RTFS_RIIVO_TELL) { finish(ctx, r, (int32_t)f->fat.position); return; }
        if (ipc->args.seek.whence == RTFS_RIIVO_SYNC) { finish(ctx, r, RTFAT_OK); return; }
        if (ipc->args.seek.whence == RTFS_SEEK_SET) base = 0;
        else if (ipc->args.seek.whence == RTFS_SEEK_CUR) base = (int64_t)f->fat.position;
        else if (ipc->args.seek.whence == RTFS_SEEK_END) base = (int64_t)f->fat.size;
        else { finish(ctx, r, RTFAT_EINVAL); return; }
        target = base + (int64_t)where;
        /* NAND files have no holes: a position past the end is refused,
         * as IOS does, so a write never has to fill a gap. */
        if (target < 0 || target > 0x7fffffffu || target > (int64_t)f->fat.size) { finish(ctx, r, RTFAT_EINVAL); return; }
        if (!r->probe) f->fat.position = (uint32_t)target;
        finish(ctx, r, (int32_t)target);
    } else {
        const uint32_t data = ipc->args.readwrite.data;
        const uint32_t length = ipc->args.readwrite.length;
        const uint32_t needed = ipc->command == RTFS_CMD_READ ? RTFS_MODE_READ : RTFS_MODE_WRITE;
        if (data == 0 && length != 0) { finish(ctx, r, RTFAT_EINVAL); return; }
        if ((f->mode & needed) == 0) { finish(ctx, r, RTFAT_EACCESS); return; }
        if (ipc->command == RTFS_CMD_WRITE && f->append && !r->probe) f->fat.position = f->fat.size;
        r->fat.file = &f->fat;
        r->fat.buffer = data;
        r->fat.length = length;
        start_fat(ctx, r, ipc->command == RTFS_CMD_READ ? RTFAT_OP_READ : RTFAT_OP_WRITE,
                  ipc->command == RTFS_CMD_READ ? RTFS_ACTION_READ : RTFS_ACTION_WRITE);
    }
}

static void begin_ioctl(struct rtfs_context* ctx, struct rtfs_request* r, const struct rtfs_ipc* ipc) {
    const uint32_t code = ipc->args.ioctl.request;
    int p;
    if (ipc->fd == RTFS_RIIVO_FD && riivo_on(ctx)) { riivo_ioctl(ctx, r, ipc); return; }
    if (fake_namespace(ipc->fd)) {
        struct rtfs_file* f;
        uint32_t slot;
        if (code != RTFS_IOCTL_GETFILESTATS) { finish(ctx, r, RTFAT_EINVAL); return; }
        f = fake_file(ctx, ipc->fd, &slot);
        if (f == 0 || ipc->args.ioctl.out == 0 || ipc->args.ioctl.out_len < 8u) { finish(ctx, r, RTFAT_EINVAL); return; }
        if (busy(ctx, r)) { finish(ctx, r, RTFAT_EACCESS); return; }
        if (!r->probe) {
            ((uint32_t*)(uintptr_t)ipc->args.ioctl.out)[0] = f->fat.size;
            ((uint32_t*)(uintptr_t)ipc->args.ioctl.out)[1] = f->fat.position;
        }
        finish(ctx, r, RTFAT_OK); return;
    }
    if (!on_fs_device(ctx, ipc->fd)) return;
    /* From here on: not ours until a path under the prefix says so. */
    if (code == RTFS_IOCTL_DELETE) {
        if (ipc->args.ioctl.in == 0 || ipc->args.ioctl.in_len < RTFS_PATH_BYTES) return;
        p = path_type(ctx, (const char*)(uintptr_t)ipc->args.ioctl.in, r->fat.name);
        if (p == PATH_OUTSIDE) return;
        if (p != PATH_FILE) { finish(ctx, r, RTFAT_EINVAL); return; }
        if (busy(ctx, r)) { finish(ctx, r, RTFAT_EACCESS); return; }
        start_fat(ctx, r, RTFAT_OP_DELETE, RTFS_ACTION_DELETE); return;
    }
    if (code == RTFS_IOCTL_RENAME) {
        const char* paths = (const char*)(uintptr_t)ipc->args.ioctl.in;
        if (paths == 0 || ipc->args.ioctl.in_len < 2u * RTFS_PATH_BYTES) return;
        p = path_type(ctx, paths, r->fat.name);
        { const int q = path_type(ctx, paths + RTFS_PATH_BYTES, r->fat.name2);
          if (p == PATH_OUTSIDE && q == PATH_OUTSIDE) return;
          if (p != PATH_FILE || q != PATH_FILE) { finish(ctx, r, RTFAT_EINVAL); return; } }
        if (busy(ctx, r)) { finish(ctx, r, RTFAT_EACCESS); return; }
        start_fat(ctx, r, RTFAT_OP_RENAME, RTFS_ACTION_RENAME); return;
    }
    if (code == RTFS_IOCTL_CREATEFILE || code == RTFS_IOCTL_CREATEDIR || code == RTFS_IOCTL_SETATTR || code == RTFS_IOCTL_GETATTR) {
        if (code == RTFS_IOCTL_GETATTR && (ipc->args.ioctl.in == 0 || ipc->args.ioctl.in_len < RTFS_PATH_BYTES)) return;
        p = (code == RTFS_IOCTL_GETATTR) ?
            path_type(ctx, (const char*)(uintptr_t)ipc->args.ioctl.in, r->fat.name) :
            valid_attr_input(ipc->args.ioctl.in, ipc->args.ioctl.in_len, r->fat.name, ctx);
        if (p == PATH_OUTSIDE) return;
        if (busy(ctx, r)) { finish(ctx, r, RTFAT_EACCESS); return; }
        if (code == RTFS_IOCTL_CREATEDIR) {
            /* The save directory exists; nothing may be created inside
             * it (one flat directory), and a bad name is a bad name. */
            if (p == PATH_DIR) finish(ctx, r, RTFAT_EEXIST);
            else if (p == PATH_FILE) finish(ctx, r, RTFAT_EACCESS);
            else finish(ctx, r, RTFAT_EINVAL);
            return;
        }
        if (code == RTFS_IOCTL_GETATTR && p == PATH_DIR) {
            if (ipc->args.ioctl.out == 0 || ipc->args.ioctl.out_len < (uint32_t)sizeof(struct rtfs_attr_block)) { finish(ctx, r, RTFAT_EINVAL); return; }
            if (!r->probe) put_attr(ipc->args.ioctl.out);
            finish(ctx, r, RTFAT_OK); return;
        }
        if (p != PATH_FILE || (code == RTFS_IOCTL_GETATTR &&
            (ipc->args.ioctl.out == 0 || ipc->args.ioctl.out_len < (uint32_t)sizeof(struct rtfs_attr_block)))) {
            finish(ctx, r, RTFAT_EINVAL); return;
        }
        if (code == RTFS_IOCTL_CREATEFILE) start_fat(ctx, r, RTFAT_OP_CREATE, RTFS_ACTION_CREATE);
        else {
            if (code == RTFS_IOCTL_GETATTR) r->out0 = ipc->args.ioctl.out;
            start_fat(ctx, r, RTFAT_OP_LOOKUP, code == RTFS_IOCTL_GETATTR ? RTFS_ACTION_ATTR : RTFS_ACTION_SETATTR);
        }
        return;
    }
}

static void begin_ioctlv(struct rtfs_context* ctx, struct rtfs_request* r, const struct rtfs_ipc* ipc) {
    struct rtfs_iovec* v;
    const uint32_t code = ipc->args.ioctlv.request;
    int p;
    if (ipc->fd == RTFS_RIIVO_FD && riivo_on(ctx)) { riivo_ioctlv(ctx, r, ipc); return; }
    if (fake_namespace(ipc->fd)) { finish(ctx, r, RTFAT_EINVAL); return; }
    if (!on_fs_device(ctx, ipc->fd)) return;
    /* Not ours until the path vector says so. */
    v = (struct rtfs_iovec*)(uintptr_t)ipc->args.ioctlv.vectors;
    if (v == 0) return;
    if (code == RTFS_IOCTL_READDIR) {
        if ((ipc->args.ioctlv.in_count != 1u || ipc->args.ioctlv.out_count != 1u) &&
            (ipc->args.ioctlv.in_count != 2u || ipc->args.ioctlv.out_count != 2u)) return;
        if (v[0].data == 0 || v[0].len < RTFS_PATH_BYTES) return;
        p = path_type(ctx, (const char*)(uintptr_t)v[0].data, r->fat.name);
        if (p == PATH_OUTSIDE) return;
        if (p == PATH_FILE) {
            /* The SDK asks "does it exist, and is it a directory?" this
             * way: IOS answers -101 for a file, -106 for nothing there. */
            if (busy(ctx, r)) { finish(ctx, r, RTFAT_EACCESS); return; }
            start_fat(ctx, r, RTFAT_OP_LOOKUP, RTFS_ACTION_ISDIR); return;
        }
        if (p != PATH_DIR) { finish(ctx, r, RTFAT_EINVAL); return; }
        if (v[1].data == 0 || v[1].len < 4u) { finish(ctx, r, RTFAT_EINVAL); return; }
        if (busy(ctx, r)) { finish(ctx, r, RTFAT_EACCESS); return; }
        if (ipc->args.ioctlv.in_count == 1u) {
            /* The count form: how many files there are, no names. */
            r->out0 = v[1].data;
            start_fat(ctx, r, RTFAT_OP_COUNT, RTFS_ACTION_LIST); return;
        }
        {
            const uint32_t count = *(uint32_t*)(uintptr_t)v[1].data;
            if ((count != 0 && count > 0xffffffffu / RTFAT_SLOT_BYTES) || v[2].data == 0 || v[2].len < count * RTFAT_SLOT_BYTES ||
                v[3].data == 0 || v[3].len < 4u) { finish(ctx, r, RTFAT_EINVAL); return; }
            r->out0 = v[3].data; r->fat.buffer = v[2].data; r->fat.length = count;
        }
        start_fat(ctx, r, RTFAT_OP_LIST, RTFS_ACTION_LIST); return;
    }
    if (code == RTFS_IOCTL_GETUSAGE) {
        if (ipc->args.ioctlv.in_count != 1u || ipc->args.ioctlv.out_count != 2u || v[0].data == 0 || v[0].len < RTFS_PATH_BYTES) return;
        p = path_type(ctx, (const char*)(uintptr_t)v[0].data, r->fat.name);
        if (p == PATH_OUTSIDE) return;
        if (p == PATH_FILE) {
            if (busy(ctx, r)) { finish(ctx, r, RTFAT_EACCESS); return; }
            start_fat(ctx, r, RTFAT_OP_LOOKUP, RTFS_ACTION_ISDIR); return;
        }
        if (p != PATH_DIR) { finish(ctx, r, RTFAT_EINVAL); return; }
        if (v[1].data == 0 || v[1].len < 4u || v[2].data == 0 || v[2].len < 4u) { finish(ctx, r, RTFAT_EINVAL); return; }
        if (busy(ctx, r)) { finish(ctx, r, RTFAT_EACCESS); return; }
        r->out0 = v[1].data; r->out1 = v[2].data;
        start_fat(ctx, r, RTFAT_OP_USAGE, RTFS_ACTION_USAGE); return;
    }
}

static void begin(struct rtfs_context* ctx, struct rtfs_request* r, const struct rtfs_ipc* ipc, uint32_t probe) {
    const uint32_t bounce = r->fat.bounce, bounce_bytes = r->fat.bounce_bytes;
    zero_bytes((uint8_t*)r, (uint32_t)sizeof(*r));
    r->fat.bounce = bounce;
    r->fat.bounce_bytes = bounce_bytes;
    r->fat.want_hidden = ipc != 0 ? ipc->hidden : 0;
    r->probe = probe;
    r->classification = RTFS_PASS_THROUGH;
    if (ctx == 0 || ipc == 0) { r->classification = RTFS_COMPLETE; r->result = RTFAT_EINVAL; return; }
    r->callback = ipc->callback;
    r->user_data = ipc->user_data;
    if (ipc->command == RTFS_CMD_OPEN) begin_open(ctx, r, ipc);
    else if (ipc->command == RTFS_CMD_CLOSE || ipc->command == RTFS_CMD_READ || ipc->command == RTFS_CMD_WRITE || ipc->command == RTFS_CMD_SEEK)
        begin_fd(ctx, r, ipc);
    else if (ipc->command == RTFS_CMD_IOCTL) begin_ioctl(ctx, r, ipc);
    else if (ipc->command == RTFS_CMD_IOCTLV) begin_ioctlv(ctx, r, ipc);
}

void rtfs_begin(struct rtfs_context* ctx, struct rtfs_request* r, const struct rtfs_ipc* ipc) { begin(ctx, r, ipc, 0); }
void rtfs_probe(struct rtfs_context* ctx, struct rtfs_request* r, const struct rtfs_ipc* ipc) { begin(ctx, r, ipc, 1); }

/* An open's file, found or just created: a descriptor for it. */
static void open_found(struct rtfs_context* ctx, struct rtfs_request* r, int32_t failure) {
    struct rtfs_file* f = free_file(ctx, &r->slot);
    if (f == 0) { finish(ctx, r, failure); return; }
    f->in_use = 1; f->mode = r->fat.length; copy_name(f->name, r->fat.name);
    f->append = r->action != RTFS_ACTION_OPEN && (r->flags & RTFS_RIIVO_APPEND) != 0;
    f->fat.first_cluster = r->fat.found.first_cluster; f->fat.size = r->fat.found.size; f->fat.position = 0;
    f->fat.entry_lba = r->fat.found.entry_lba; f->fat.entry_index = r->fat.found.entry_index;
    f->fat.cache_index = 0; f->fat.cache_cluster = 0;
    finish(ctx, r, make_fd(f, r->slot));
}

/* A file device operation ended: 1 when it started another. */
static int riivo_after(struct rtfs_context* ctx, struct rtfs_request* r) {
    const int32_t res = r->fat.result;
    const struct rtfat_dirent* found = &r->fat.found;
    const uint32_t is_dir = (found->attributes & 0x10u) != 0;
    const uint32_t folder = found->first_cluster != 0 ? found->first_cluster : ctx->volume.root_cluster;
    switch (r->action) {
        case RTFS_ACTION_WALK:
            if (res != RTFAT_OK || !is_dir) break;
            r->walk_dir = folder;
            riivo_walk(ctx, r);
            return (int)r->active;
        case RTFS_ACTION_R_OPEN:
            if (res == RTFAT_OK) {
                /* O_EXCL wants a new file; truncation is not served. */
                if ((r->flags & (RTFS_RIIVO_CREAT | RTFS_RIIVO_EXCL)) == (RTFS_RIIVO_CREAT | RTFS_RIIVO_EXCL) ||
                    ((r->flags & RTFS_RIIVO_TRUNC) && found->size != 0)) break;
                open_found(ctx, r, RTFS_RIIVO_NOT_OPENED);
                return 0;
            }
            if (res == RTFAT_ENOENT && (r->flags & RTFS_RIIVO_CREAT)) {
                start_fat(ctx, r, RTFAT_OP_CREATE, RTFS_ACTION_R_OPEN_CREATE);
                return 1;
            }
            break;
        case RTFS_ACTION_R_OPEN_CREATE:
            if (res != RTFAT_OK) break;
            open_found(ctx, r, RTFS_RIIVO_NOT_OPENED);
            return 0;
        case RTFS_ACTION_R_CREATEFILE: /* an existing file is left as it is */
            if (res != RTFAT_OK && res != RTFAT_EEXIST) break;
            finish(ctx, r, RTFAT_OK);
            return 0;
        case RTFS_ACTION_R_CREATEDIR:
        case RTFS_ACTION_R_DELETE:
            if (res != RTFAT_OK) break;
            finish(ctx, r, RTFAT_OK);
            return 0;
        case RTFS_ACTION_R_OPENDIR:
            if (res != RTFAT_OK || !is_dir) break;
            riivo_opendir(ctx, r, folder);
            return 0;
        case RTFS_ACTION_R_STAT:
            if (res != RTFAT_OK) break;
            put_stats(r->out0, found->first_cluster, is_dir ? 0 : found->size, is_dir);
            finish(ctx, r, RTFAT_OK);
            return 0;
        case RTFS_ACTION_R_NEXTDIR: {
            struct rtfs_dir* d = &ctx->dirs[r->slot];
            uint8_t* name = (uint8_t*)(uintptr_t)r->out0;
            uint32_t i;
            if (res != RTFAT_OK) break;
            d->cursor_cluster = r->fat.cursor_cluster;
            d->cursor_sector = r->fat.cursor_sector;
            d->cursor_index = r->fat.cursor_index;
            for (i = 0; i + 1u < r->fat.length && found->name[i] != 0; ++i) name[i] = (uint8_t)found->name[i];
            name[i] = 0;
            put_stats(r->out1, found->first_cluster, is_dir ? 0 : found->size, is_dir);
            finish(ctx, r, RTFAT_OK);
            return 0;
        }
        default:
            break;
    }
    finish(ctx, r, riivo_failure(r));
    return 0;
}

int rtfs_step(struct rtfs_context* ctx, struct rtfs_request* r) {
    int step;
    if (ctx == 0 || r == 0 || r->classification != RTFS_NEEDS_IO || !r->active) return RTFAT_DONE;
    step = rtfat_step(&ctx->volume, &r->fat);
    if (step == RTFAT_IO) return RTFAT_IO;
    if (r->action >= RTFS_ACTION_WALK) return riivo_after(ctx, r) ? rtfs_step(ctx, r) : RTFAT_DONE;
    if (r->fat.result >= RTFAT_OK) {
        if (r->action == RTFS_ACTION_OPEN) {
            open_found(ctx, r, RTFAT_ENOINODES);
            return RTFAT_DONE;
        }
        if (r->action == RTFS_ACTION_ISDIR) { finish(ctx, r, RTFAT_EINVAL); return RTFAT_DONE; }  /* a file, not a directory */
        if (r->action == RTFS_ACTION_RENAME_REPLACE) {
            /* The destination is gone: the rename itself now. */
            copy_name(r->fat.name, r->saved_name);
            r->action = RTFS_ACTION_RENAME;
            rtfat_begin(&r->fat, RTFAT_OP_RENAME);
            return rtfs_step(ctx, r);
        }
        if (r->action == RTFS_ACTION_ATTR) put_attr(r->out0);
        else if (r->action == RTFS_ACTION_LIST) *(uint32_t*)(uintptr_t)r->out0 = (uint32_t)r->fat.result;
        else if (r->action == RTFS_ACTION_USAGE) {
            *(uint32_t*)(uintptr_t)r->out0 = r->fat.usage_blocks;
            *(uint32_t*)(uintptr_t)r->out1 = (uint32_t)r->fat.result;
        }
    }
    if (r->action == RTFS_ACTION_RENAME && r->fat.result == RTFAT_EEXIST) {
        /* IOS replaces an existing destination: delete it, then rename. */
        copy_name(r->saved_name, r->fat.name);
        copy_name(r->fat.name, r->fat.name2);
        r->action = RTFS_ACTION_RENAME_REPLACE;
        rtfat_begin(&r->fat, RTFAT_OP_DELETE);
        return rtfs_step(ctx, r);
    }
    if ((r->action == RTFS_ACTION_LIST || r->action == RTFS_ACTION_USAGE) && r->fat.result >= RTFAT_OK) finish(ctx, r, RTFAT_OK);
    else finish(ctx, r, r->fat.result);
    return RTFAT_DONE;
}
