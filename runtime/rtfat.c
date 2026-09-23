/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Resumable FAT32 engine (see rtfat.h). Every state function below either
 * changes op->state and returns RT_CONT (run the next state now), issues
 * a transfer and returns RT_IO (come back when it has happened) or ends
 * the operation with RT_END. Data the engine needs is read into the
 * record's own sector buffers; nothing lives beyond the call except the
 * record and the volume's allocation hint.
 *
 * Sub-machines (the directory scan, the chain walk, cluster allocation,
 * entry writing, entry deletion) are entered by setting their first state
 * and `next_state`, the state to continue with when they finish.
 */
#include "rtfat.h"

#define RT_CONT 0
#define RT_IO 1
#define RT_END 2

#define FAT_MASK 0x0FFFFFFFu
#define FAT_EOC 0x0FFFFFFFu
#define FAT_BAD 0x0FFFFFF7u
#define ENTRY_BYTES 32u
#define ENTRIES_PER_SECTOR 16u
#define ATTR_READ_ONLY 0x01u
#define ATTR_HIDDEN 0x02u
#define ATTR_SYSTEM 0x04u
#define ATTR_LABEL 0x08u
#define ATTR_DIRECTORY 0x10u
#define ATTR_ARCHIVE 0x20u
#define ATTR_LFN 0x0Fu
#define NT_LOWER_BASE 0x08u
#define NT_LOWER_EXT 0x10u
#define USAGE_BLOCK 16384u /* the NAND's cluster, what ISFS GetUsage counts */

/* States. */
enum {
    S_IDLE = 0,
    /* directory scan */
    S_SCAN_READ,
    S_SCAN_PROCESS,
    S_SCAN_NEXT_CLUSTER,
    /* chain walk */
    S_WALK,
    /* allocation */
    S_ALLOC_SCAN,
    S_ALLOC_MARK_WRITE,
    S_ALLOC_MARK_WRITTEN,
    S_ALLOC_LINK,
    S_ALLOC_LINK_WRITE,
    S_ALLOC_LINK_WRITTEN,
    /* writing the new entries */
    S_ENTRY_READ,
    S_ENTRY_FILL,
    S_ENTRY_WRITTEN,
    /* marking entries deleted */
    S_MARK_READ1,
    S_MARK_FILL1,
    S_MARK_READ2,
    S_MARK_FILL2,
    S_MARK_DONE,
    /* releasing a chain */
    S_FREE_LOOP,
    S_FREE_WRITE,
    S_FREE_WRITTEN,
    /* operations */
    S_LOOKUP_DONE,
    S_READ_NEXT,
    S_READ_CHUNK,
    S_READ_COPY,
    S_WRITE_NEXT,
    S_WRITE_CHUNK,
    S_WRITE_OVERLAY,
    S_WRITE_WRITTEN,
    S_WRITE_FINISH,
    S_WRITE_ENTRY_READ,
    S_WRITE_ENTRY_UPDATE,
    S_WRITE_ENTRY_WRITTEN,
    S_CREATE_SCANNED,
    S_CREATE_WRITTEN,
    S_DELETE_FOUND,
    S_DELETE_MARKED,
    S_RENAME_FOUND,
    S_RENAME_SCANNED,
    S_RENAME_WRITTEN,
    S_RENAME_MARKED,
    S_LIST_DONE,
    S_DIR_GAP_WRITTEN,
    S_DIR_NEXT,
    S_DIR_GROWN,
    S_DIR_ZERO,
    /* A failed directory extension is unwound explicitly.  Each copy is
     * read and rewritten independently: the parent tail is restored to
     * EOC first, then the candidate is freed.  A failed recovery transfer
     * never stops the sweep of the remaining copies. */
    S_DIR_RECOVER_PARENT_READ,
    S_DIR_RECOVER_PARENT_READ_DONE,
    S_DIR_RECOVER_PARENT_WRITE_DONE,
    S_DIR_RECOVER_CANDIDATE_READ,
    S_DIR_RECOVER_CANDIDATE_READ_DONE,
    S_DIR_RECOVER_CANDIDATE_WRITE_DONE,
    S_DIR_RECOVER_DONE,
    S_DONE
};

/* scan_mode */
#define SCAN_FIND 1   /* op->new_name -> op->found; end -> ENOENT */
#define SCAN_CREATE 2 /* collisions with op->new_name / short11, first free run; end -> continue */
#define SCAN_LIST 3   /* count files (and list them) */

/* --- small helpers -------------------------------------------------------- */

static uint32_t rd16(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}
static uint32_t rd32(const uint8_t* p) {
    return rd16(p) | (rd16(p + 2) << 16);
}
static void wr16(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void wr32(uint8_t* p, uint32_t v) {
    wr16(p, v & 0xFFFFu);
    wr16(p + 2, v >> 16);
}
static void copy_bytes(uint8_t* d, const uint8_t* s, uint32_t n) {
    while (n--) *d++ = *s++;
}
static void zero_bytes(uint8_t* d, uint32_t n) {
    while (n--) *d++ = 0;
}
static int to_lower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}
static int to_upper(int c) {
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}
static uint32_t str_len(const char* s) {
    uint32_t n = 0;
    while (s[n]) ++n;
    return n;
}
static int names_equal(const char* a, const char* b) {
    while (*a && *b) {
        if (to_lower((unsigned char)*a) != to_lower((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}
/* Characters a long name may hold (what ISFS names may hold too). */
static int lfn_char_ok(int c) {
    if (c < 0x20 || c > 0x7E) return 0;
    switch (c) {
        case '"':
        case '*':
        case '/':
        case ':':
        case '<':
        case '>':
        case '?':
        case '\\':
        case '|':
        case ' ':
            return 0;
        default:
            return 1;
    }
}
/* Characters a short name may hold (case aside). */
static int short_char_ok(int c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return 1;
    switch (c) {
        case '$':
        case '%':
        case '\'':
        case '-':
        case '_':
        case '@':
        case '~':
        case '`':
        case '!':
        case '(':
        case ')':
        case '{':
        case '}':
        case '^':
        case '#':
        case '&':
            return 1;
        default:
            return 0;
    }
}

uint32_t rtfat_short_checksum(const uint8_t* name11) {
    uint32_t sum = 0;
    uint32_t i;
    for (i = 0; i < 11; ++i) sum = (((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + name11[i]) & 0xFFu;
    return sum;
}

uint32_t rtfat_cluster_bytes(const struct rtfat_volume* vol) {
    return vol->sectors_per_cluster * RTFAT_SECTOR_BYTES;
}

uint32_t rtfat_cluster_lba(const struct rtfat_volume* vol, uint32_t cluster) {
    return vol->data_lba + (cluster - 2u) * vol->sectors_per_cluster;
}

static int cluster_valid(const struct rtfat_volume* vol, uint32_t c) {
    return c >= 2u && c < vol->cluster_count + 2u;
}

int rtfat_valid_name(const char* name) {
    uint32_t n = 0;
    if (name == 0) return 0;
    while (name[n]) {
        if (!lfn_char_ok((unsigned char)name[n])) return 0;
        if (++n > RTFAT_NAME_MAX) return 0;
    }
    if (n == 0) return 0;
    if (name[0] == '.' && (n == 1 || (n == 2 && name[1] == '.'))) return 0;
    return 1;
}

/* Splits `name` at its last dot: base [0, dot), extension (dot, end).
 * Returns the base length; *ext_start is the extension's start (== the
 * length of the name when there is none), *ext_len its length. */
static uint32_t split_name(const char* name, uint32_t* ext_start, uint32_t* ext_len) {
    const uint32_t n = str_len(name);
    uint32_t dot = n;
    uint32_t i;
    for (i = 0; i < n; ++i) {
        if (name[i] == '.') dot = i;
    }
    if (dot == n) {
        *ext_start = n;
        *ext_len = 0;
        return n;
    }
    *ext_start = dot + 1;
    *ext_len = n - dot - 1;
    return dot;
}

int rtfat_fits_short(const char* name, uint8_t* out11, uint8_t* nt_flags) {
    uint32_t ext_start, ext_len;
    const uint32_t base_len = split_name(name, &ext_start, &ext_len);
    uint32_t i, k;
    int fits = base_len >= 1 && base_len <= 8 && ext_len <= 3;
    int base_lower = 0, base_upper = 0, ext_lower = 0, ext_upper = 0;
    for (i = 0; i < 11; ++i) out11[i] = ' ';
    *nt_flags = 0;
    if (ext_start == str_len(name) && base_len != ext_start) fits = 0; /* a trailing dot */
    /* Only one dot, every character 8.3-clean, each part in one case. */
    for (i = 0; fits && i < base_len; ++i) {
        const int c = (unsigned char)name[i];
        if (!short_char_ok(c)) fits = 0;
        if (c >= 'a' && c <= 'z') base_lower = 1;
        if (c >= 'A' && c <= 'Z') base_upper = 1;
    }
    for (i = 0; fits && i < ext_len; ++i) {
        const int c = (unsigned char)name[ext_start + i];
        if (!short_char_ok(c)) fits = 0;
        if (c >= 'a' && c <= 'z') ext_lower = 1;
        if (c >= 'A' && c <= 'Z') ext_upper = 1;
    }
    if (fits && ((base_lower && base_upper) || (ext_lower && ext_upper))) fits = 0;
    if (fits) {
        for (i = 0; i < base_len; ++i) out11[i] = (uint8_t)to_upper((unsigned char)name[i]);
        for (i = 0; i < ext_len; ++i) out11[8 + i] = (uint8_t)to_upper((unsigned char)name[ext_start + i]);
        if (base_lower) *nt_flags |= NT_LOWER_BASE;
        if (ext_lower) *nt_flags |= NT_LOWER_EXT;
        return 1;
    }
    /* The alias: up to six clean characters of the base, "~1", up to
     * three clean characters of the extension. */
    k = 0;
    for (i = 0; i < base_len && k < 6; ++i) {
        const int c = (unsigned char)name[i];
        if (short_char_ok(c)) out11[k++] = (uint8_t)to_upper(c);
    }
    if (k == 0) out11[k++] = '_';
    out11[k++] = '~';
    out11[k++] = '1';
    k = 0;
    for (i = 0; i < ext_len && k < 3; ++i) {
        const int c = (unsigned char)name[ext_start + i];
        if (short_char_ok(c)) out11[8 + k++] = (uint8_t)to_upper(c);
    }
    return 0;
}

/* --- transfers --------------------------------------------------------------- */

static int issue(struct rtfat_op* op, uint32_t lba, uint32_t count, uint32_t write, const void* buffer) {
    op->io_lba = lba;
    op->io_count = count;
    op->io_write = write;
    op->io_buffer = (uint32_t)(uintptr_t)buffer;
    op->io_status = 0;
    op->io_pending = 1;
    return RT_IO;
}

static int finish(struct rtfat_op* op, int32_t result) {
    op->result = result;
    op->state = S_DONE;
    return RT_END;
}
/* finish() for rtfat_step's own early exits. */
static int done_now(struct rtfat_op* op, int32_t result) {
    finish(op, result);
    return RTFAT_DONE;
}

/* --- the FAT ------------------------------------------------------------------ */

static uint32_t fat_sector_of(const struct rtfat_volume* vol, uint32_t cluster) {
    return vol->fat_lba + (cluster >> 7); /* 128 entries per sector */
}
static uint32_t fat_offset_of(uint32_t cluster) {
    return (cluster & 127u) * 4u;
}

/* The FAT entry of `cluster`: 1 with *value set when the sector is
 * cached, 0 after issuing its read (the caller returns RT_IO and re-enters
 * the same state). The cache is marked valid at issue time: a failed
 * transfer ends the operation, and a new operation starts with no cache. */
static int fat_get(const struct rtfat_volume* vol, struct rtfat_op* op, uint32_t cluster, uint32_t* value) {
    const uint32_t lba = fat_sector_of(vol, cluster);
    if (op->fat_cached_lba == lba) {
        *value = rd32(op->fat_sector + fat_offset_of(cluster)) & FAT_MASK;
        return 1;
    }
    op->fat_cached_lba = lba;
    issue(op, lba, 1, 0, op->fat_sector);
    return 0;
}

static void fat_set_cached(struct rtfat_op* op, uint32_t cluster, uint32_t value) {
    uint8_t* p = op->fat_sector + fat_offset_of(cluster);
    wr32(p, (rd32(p) & 0xF0000000u) | (value & FAT_MASK));
}

/* Writes the cached FAT sector to copy `copy`. */
static int fat_issue_write(const struct rtfat_volume* vol, struct rtfat_op* op, uint32_t copy) {
    return issue(op, op->fat_cached_lba + copy * vol->fat_sectors, 1, 1, op->fat_sector);
}

static int fat_issue_copy_read(const struct rtfat_volume* vol, struct rtfat_op* op, uint32_t copy, uint32_t cluster) {
    return issue(op, fat_sector_of(vol, cluster) + copy * vol->fat_sectors, 1, 0, op->fat_sector);
}

static int fat_issue_copy_write(const struct rtfat_volume* vol, struct rtfat_op* op, uint32_t copy, uint32_t cluster) {
    return issue(op, fat_sector_of(vol, cluster) + copy * vol->fat_sectors, 1, 1, op->fat_sector);
}

static int recovery_state(uint32_t state) {
    return state >= S_DIR_RECOVER_PARENT_READ && state <= S_DIR_RECOVER_DONE;
}

/* Ordinary file-chain allocation/free has no rollback record.  A failed
 * mirrored FAT write may have reached one copy, so do not permit another
 * mutation against an image whose agreement cannot be proved. */
static int ordinary_fat_mutation_write_state(uint32_t state) {
    return state == S_ALLOC_MARK_WRITTEN || state == S_ALLOC_LINK_WRITTEN || state == S_FREE_WRITTEN;
}

/* The candidate is not reachable until its zeroing completed and the
 * parent link was written, but a failed card request is ambiguous.  Write
 * both invariants back in every copy before returning the original EIO. */
static void begin_dir_recovery(struct rtfat_volume* vol, struct rtfat_op* op) {
    if (op->alloc_cluster < vol->alloc_hint) vol->alloc_hint = op->alloc_cluster;
    op->dir_grow = 0;
    op->recovery_copy = 0;
    op->recovery_failed = 0;
    op->fat_cached_lba = 0;
    op->state = S_DIR_RECOVER_PARENT_READ;
}

/* --- directory entries ------------------------------------------------------- */

static uint32_t entry_cluster(const uint8_t* e) {
    return (rd16(e + 0x14) << 16) | rd16(e + 0x1A);
}

static void entry_set_cluster(uint8_t* e, uint32_t cluster) {
    wr16(e + 0x14, cluster >> 16);
    wr16(e + 0x1A, cluster & 0xFFFFu);
}

/* The 8.3 name as displayed (NT case flags applied), at most 12 characters. */
static void decode_short(const uint8_t* e, char* out) {
    uint32_t n = 0, i, base_len = 8, ext_len = 3;
    const uint32_t flags = e[12];
    while (base_len > 0 && e[base_len - 1] == ' ') --base_len;
    while (ext_len > 0 && e[8 + ext_len - 1] == ' ') --ext_len;
    for (i = 0; i < base_len; ++i) {
        int c = e[i];
        if (i == 0 && c == 0x05) c = 0xE5;
        out[n++] = (char)((flags & NT_LOWER_BASE) ? to_lower(c) : c);
    }
    if (ext_len > 0) {
        out[n++] = '.';
        for (i = 0; i < ext_len; ++i) {
            const int c = e[8 + i];
            out[n++] = (char)((flags & NT_LOWER_EXT) ? to_lower(c) : c);
        }
    }
    out[n] = 0;
}

static void reset_lfn(struct rtfat_op* op) {
    op->lfn_expect = 0;
    op->lfn_count = 0;
    op->lfn_ok = 0;
}

/* Collects one long-name entry's 13 units into op->lfn. */
static void take_lfn(struct rtfat_op* op, const uint8_t* e, uint32_t lba, uint32_t index) {
    const uint32_t seq = e[0];
    const uint32_t part = seq & 0x1Fu;
    uint32_t i;
    if (part == 0 || part > 20) {
        reset_lfn(op);
        return;
    }
    if (seq & 0x40u) {
        op->lfn_expect = part;
        op->lfn_sum = e[13];
        op->lfn_count = 0;
        op->lfn_lba = lba;
        op->lfn_index = index;
        op->lfn_ok = 1;
        for (i = 0; i <= RTFAT_NAME_MAX; ++i) op->lfn[i] = 0;
    } else if (op->lfn_expect == 0 || part != op->lfn_expect || e[13] != op->lfn_sum) {
        reset_lfn(op);
        return;
    }
    op->lfn_count++;
    op->lfn_expect = part - 1;
    for (i = 0; i < 13; ++i) {
        const uint32_t at = i < 5 ? 1 + i * 2 : (i < 11 ? 14 + (i - 5) * 2 : 28 + (i - 11) * 2);
        const uint32_t u = rd16(e + at);
        const uint32_t pos = (part - 1) * 13 + i;
        if (u == 0 || u == 0xFFFFu) continue; /* terminator and padding */
        if (pos >= RTFAT_NAME_MAX || u < 0x20 || u >= 0x80) {
            op->lfn_ok = 0; /* too long for a path, or not ASCII: the entry goes by its alias */
        } else {
            op->lfn[pos] = (char)u;
        }
    }
}

/* Whether the short entry `e` is the alias pattern base~N.ext of op->short11 (which holds ~1). */
static int alias_digit(const struct rtfat_op* op, const uint8_t* e) {
    uint32_t tilde = 0;
    uint32_t i;
    while (tilde < 8 && op->short11[tilde] != '~') ++tilde;
    if (tilde >= 7) return 0;
    for (i = 0; i < tilde; ++i) {
        if (e[i] != op->short11[i]) return 0;
    }
    if (e[tilde] != '~' || e[tilde + 1] < '1' || e[tilde + 1] > '9') return 0;
    for (i = tilde + 2; i < 8; ++i) {
        if (e[i] != ' ') return 0;
    }
    for (i = 8; i < 11; ++i) {
        if (e[i] != op->short11[i]) return 0;
    }
    return e[tilde + 1] - '0';
}

/* One in-use short entry during the scan: 0 to go on, 1 to stop (the
 * operation's state has been set). */
static int on_short_entry(struct rtfat_op* op, const uint8_t* e, uint32_t lba, uint32_t index) {
    struct rtfat_dirent d;
    const uint32_t attr = e[11];
    const int long_valid = op->lfn_count > 0 && op->lfn_expect == 0 && op->lfn_ok &&
                           rtfat_short_checksum(e) == op->lfn_sum && op->lfn[0] != 0;
    uint32_t i;
    if ((attr & ATTR_LABEL) || ((attr & ATTR_HIDDEN) && !op->want_hidden && op->scan_mode != SCAN_CREATE)) {
        reset_lfn(op);
        return 0;
    }
    d.first_cluster = entry_cluster(e);
    d.size = rd32(e + 0x1C);
    d.attributes = attr;
    d.entry_lba = lba;
    d.entry_index = index;
    decode_short(e, d.alias);
    if (long_valid) {
        d.lfn_lba = op->lfn_lba;
        d.lfn_index = op->lfn_index;
        d.lfn_count = op->lfn_count;
        for (i = 0; i <= RTFAT_NAME_MAX; ++i) d.name[i] = op->lfn[i];
    } else {
        d.lfn_lba = lba;
        d.lfn_index = index;
        d.lfn_count = 0;
        for (i = 0; i <= RTFAT_LIST_NAME_MAX; ++i) d.name[i] = d.alias[i];
    }
    reset_lfn(op);
    switch (op->scan_mode) {
        case SCAN_FIND:
            if (!(attr & ATTR_DIRECTORY) &&
                (names_equal(d.name, op->new_name) ||
                 (str_len(d.name) > RTFAT_LIST_NAME_MAX && names_equal(d.alias, op->new_name)))) {
                op->found = d;
                op->state = op->next_state;
                return 1;
            }
            return 0;
        case SCAN_CREATE: {
            int same_short = 1;
            for (i = 0; i < 11; ++i) {
                if (e[i] != op->short11[i]) same_short = 0;
            }
            if (names_equal(d.name, op->new_name) || (same_short && !op->use_lfn)) {
                finish(op, RTFAT_EEXIST);
                return 1;
            }
            if (op->use_lfn) {
                const int digit = alias_digit(op, e);
                if (digit) op->short_mask |= 1u << (digit - 1);
            }
            return 0;
        }
        default: /* SCAN_LIST */
            if (!(attr & ATTR_DIRECTORY)) {
                if (op->kind == RTFAT_OP_LIST && op->count < op->length) {
                    /* As IOS answers ReadDir: the names one after another,
                     * each NUL-terminated (Dolphin's IOS, libogc's callers). */
                    uint8_t* slot = (uint8_t*)(uintptr_t)op->buffer + op->list_bytes;
                    const char* shown = str_len(d.name) > RTFAT_LIST_NAME_MAX ? d.alias : d.name;
                    for (i = 0; i < RTFAT_LIST_NAME_MAX && shown[i]; ++i) slot[i] = (uint8_t)shown[i];
                    slot[i] = 0;
                    op->list_bytes += i + 1;
                }
                op->count++;
                op->usage_blocks += (d.size + USAGE_BLOCK - 1) / USAGE_BLOCK;
            }
            return 0;
    }
}

/* Notes a free entry at (lba, index) for SCAN_CREATE. */
static void note_free(struct rtfat_op* op, uint32_t lba, uint32_t index) {
    if (op->scan_mode != SCAN_CREATE || op->free_lba != 0) return;
    op->free_run++;
    if (op->free_run >= op->need_entries) {
        op->free_lba = lba;
        op->free_index = index + 1 - op->need_entries;
    }
}

/* Processes the 16 entries of the sector just read. Returns 1 when the
 * scan stops here (state set), 0 to continue with the next sector. */
static int scan_process(struct rtfat_op* op) {
    uint32_t i;
    op->free_run = 0; /* runs are counted within one sector */
    for (i = 0; i < ENTRIES_PER_SECTOR; ++i) {
        const uint8_t* e = op->sector + i * ENTRY_BYTES;
        if (e[0] == 0x00) {
            uint32_t j;
            reset_lfn(op);
            for (j = i; j < ENTRIES_PER_SECTOR; ++j) note_free(op, op->scan_lba, j);
            op->scan_end = 1;
            return 0; /* the caller ends the scan */
        }
        if (e[0] == 0xE5) {
            reset_lfn(op);
            note_free(op, op->scan_lba, i);
            continue;
        }
        op->free_run = 0;
        if ((e[11] & 0x3Fu) == ATTR_LFN) {
            take_lfn(op, e, op->scan_lba, i);
            continue;
        }
        if (on_short_entry(op, e, op->scan_lba, i)) return 1;
    }
    return 0;
}

static void scan_begin(const struct rtfat_volume* vol, struct rtfat_op* op, uint32_t mode, uint32_t next_state) {
    op->scan_mode = mode;
    op->next_state = next_state;
    op->scan_cluster = vol->dir_cluster;
    op->scan_sector = 0;
    op->scan_lba = rtfat_cluster_lba(vol, vol->dir_cluster);
    op->scan_clusters = 1;
    op->scan_end = 0;
    op->free_lba = 0;
    op->free_index = 0;
    op->free_run = 0;
    op->short_mask = 0;
    op->count = 0;
    op->list_bytes = 0;
    op->usage_blocks = 0;
    reset_lfn(op);
    op->state = S_SCAN_READ;
}

/* What a scan does when the directory is exhausted. A creation that
 * found no run of free entries goes past this sector: the rest of the
 * cluster when there is one, else the next cluster of the directory
 * (S_DIR_NEXT: an unused one already chained, or a new one, zeroed).
 * When the scan stopped at an end-of-directory entry, that entry and
 * the ones after it are first marked deleted, so that later scans read
 * on to the entries beyond this sector (FAT: 0x00 ends the directory). */
static int scan_ended(const struct rtfat_volume* vol, struct rtfat_op* op) {
    (void)vol;
    switch (op->scan_mode) {
        case SCAN_FIND:
            return finish(op, RTFAT_ENOENT);
        case SCAN_CREATE:
            if (op->free_lba == 0) {
                op->grow_next = op->next_state;
                if (op->scan_end) {
                    uint32_t i, past = 0;
                    for (i = 0; i < ENTRIES_PER_SECTOR; ++i) {
                        uint8_t* e = op->sector + i * ENTRY_BYTES;
                        if (e[0] == 0x00) past = 1;
                        if (past) e[0] = 0xE5;
                    }
                    op->state = S_DIR_GAP_WRITTEN;
                    return issue(op, op->scan_lba, 1, 1, op->sector);
                }
                op->state = S_DIR_NEXT;
                return RT_CONT;
            }
            op->state = op->next_state;
            return RT_CONT;
        default:
            op->state = op->next_state;
            return RT_CONT;
    }
}

/* --- entry writing ----------------------------------------------------------- */

/* Prepares the short form of op->new_name for creation. Returns 0 on an
 * invalid name. */
static int prepare_new_entry(struct rtfat_op* op, const char* name, uint32_t cluster, uint32_t size,
                             uint32_t attr) {
    if (!rtfat_valid_name(name)) return 0;
    op->new_name = name;
    op->new_cluster = cluster;
    op->new_size = size;
    op->new_attr = (uint8_t)attr;
    op->use_lfn = rtfat_fits_short(name, op->short11, &op->nt_flags) ? 0 : 1;
    op->need_entries = (uint8_t)(op->use_lfn ? 1 + (str_len(name) + 12) / 13 : 1);
    return 1;
}

static uint32_t hidden_attribute(const struct rtfat_op* op, uint32_t attr) {
    if (op->want_hidden == 1u) return attr | ATTR_HIDDEN;
    if (op->want_hidden == 2u) return attr & ~ATTR_HIDDEN;
    return attr;
}

static void fill_lfn_entry(uint8_t* e, const char* name, uint32_t part, uint32_t parts, uint32_t sum) {
    const uint32_t n = str_len(name);
    uint32_t i;
    zero_bytes(e, ENTRY_BYTES);
    e[0] = (uint8_t)(part | (part == parts ? 0x40u : 0u));
    e[11] = ATTR_LFN;
    e[13] = (uint8_t)sum;
    for (i = 0; i < 13; ++i) {
        const uint32_t pos = (part - 1) * 13 + i;
        const uint32_t at = i < 5 ? 1 + i * 2 : (i < 11 ? 14 + (i - 5) * 2 : 28 + (i - 11) * 2);
        uint32_t u;
        if (pos < n) {
            u = (uint8_t)name[pos];
        } else if (pos == n) {
            u = 0;
        } else {
            u = 0xFFFFu;
        }
        wr16(e + at, u);
    }
}

/* Writes the entries for op->new_name at (free_lba, free_index) into the
 * sector buffer; fills op->found. */
static void fill_entries(struct rtfat_op* op) {
    const uint32_t parts = op->need_entries - 1;
    uint32_t k;
    uint8_t* e;
    if (op->use_lfn) {
        /* The alias digit: the lowest ~N nobody uses (all nine taken is a
         * refusal a real volume answers with a longer alias; ISFS never
         * holds that many collisions, so ~9 is reused). */
        uint32_t digit = 1, tilde = 0;
        while (digit < 9 && (op->short_mask & (1u << (digit - 1)))) ++digit;
        while (op->short11[tilde] != '~') ++tilde;
        op->short11[tilde + 1] = (uint8_t)('0' + digit);
    }
    for (k = 0; k < parts; ++k) {
        e = op->sector + (op->free_index + k) * ENTRY_BYTES;
        fill_lfn_entry(e, op->new_name, parts - k, parts, rtfat_short_checksum(op->short11));
    }
    e = op->sector + (op->free_index + parts) * ENTRY_BYTES;
    zero_bytes(e, ENTRY_BYTES);
    copy_bytes(e, op->short11, 11);
    e[11] = op->new_attr;
    e[12] = op->nt_flags;
    wr16(e + 0x10, 0x0021); /* created 1980-01-01 */
    wr16(e + 0x12, 0x0021); /* accessed */
    wr16(e + 0x18, 0x0021); /* written */
    entry_set_cluster(e, op->new_cluster);
    wr32(e + 0x1C, op->new_size);
    op->found.first_cluster = op->new_cluster;
    op->found.size = op->new_size;
    op->found.attributes = op->new_attr;
    op->found.entry_lba = op->free_lba;
    op->found.entry_index = op->free_index + parts;
    op->found.lfn_lba = op->free_lba;
    op->found.lfn_index = op->free_index;
    op->found.lfn_count = parts;
    for (k = 0; k <= RTFAT_NAME_MAX; ++k) op->found.name[k] = op->new_name[k];
    decode_short(e, op->found.alias);
}

/* --- the chain walk --------------------------------------------------------- */

static void walk_begin(struct rtfat_op* op, uint32_t target, uint32_t next_state) {
    const struct rtfat_file* f = op->file;
    op->walk_target = target;
    op->walk_prev = 0;
    op->next_state = next_state;
    if (f->cache_cluster != 0 && f->cache_index <= target) {
        op->walk_index = f->cache_index;
        op->walk_cluster = f->cache_cluster;
    } else {
        op->walk_index = 0;
        op->walk_cluster = f->first_cluster;
    }
    op->state = S_WALK;
}

/* --- the operations --------------------------------------------------------- */

static void data_chunk(const struct rtfat_volume* vol, struct rtfat_op* op, uint32_t abs, uint32_t remaining) {
    const uint32_t cb = rtfat_cluster_bytes(vol);
    const uint32_t off = abs % cb;
    const uint32_t skip = off % RTFAT_SECTOR_BYTES;
    uint32_t max = remaining < cb - off ? remaining : cb - off;
    uint32_t count = (skip + max + RTFAT_SECTOR_BYTES - 1) / RTFAT_SECTOR_BYTES;
    const uint32_t bounce_sectors = op->bounce_bytes / RTFAT_SECTOR_BYTES;
    if (count > bounce_sectors) count = bounce_sectors;
    if (max > count * RTFAT_SECTOR_BYTES - skip) max = count * RTFAT_SECTOR_BYTES - skip;
    op->chunk_lba = rtfat_cluster_lba(vol, op->walk_cluster) + off / RTFAT_SECTOR_BYTES;
    op->chunk_count = count;
    op->chunk_skip = skip;
    op->chunk_bytes = max;
}

static int run_state(struct rtfat_volume* vol, struct rtfat_op* op) {
    uint32_t v;
    switch (op->state) {
        /* ---- directory scan ---- */
        case S_SCAN_READ:
            op->state = S_SCAN_PROCESS;
            return issue(op, op->scan_lba, 1, 0, op->sector);
        case S_SCAN_PROCESS:
            if (scan_process(op)) return RT_CONT;
            if (op->scan_end) return scan_ended(vol, op);
            if (++op->scan_sector < vol->sectors_per_cluster) {
                op->scan_lba++;
                op->state = S_SCAN_READ;
                return RT_CONT;
            }
            op->state = S_SCAN_NEXT_CLUSTER;
            return RT_CONT;
        case S_SCAN_NEXT_CLUSTER:
            if (!fat_get(vol, op, op->scan_cluster, &v)) return RT_IO;
            if (v >= 0x0FFFFFF8u) return scan_ended(vol, op);
            if (!cluster_valid(vol, v) || ++op->scan_clusters > vol->cluster_count) return finish(op, RTFAT_ECORRUPT);
            op->scan_cluster = v;
            op->scan_sector = 0;
            op->scan_lba = rtfat_cluster_lba(vol, v);
            op->state = S_SCAN_READ;
            return RT_CONT;

        /* ---- chain walk ---- */
        case S_WALK:
            while (op->walk_cluster != 0 && op->walk_index < op->walk_target) {
                if (!cluster_valid(vol, op->walk_cluster)) return finish(op, RTFAT_ECORRUPT);
                if (!fat_get(vol, op, op->walk_cluster, &v)) return RT_IO;
                if (v >= 0x0FFFFFF8u) {
                    op->walk_prev = op->walk_cluster;
                    op->walk_cluster = 0;
                    break;
                }
                if (!cluster_valid(vol, v) || op->walk_index + 1 > vol->cluster_count) return finish(op, RTFAT_ECORRUPT);
                op->walk_cluster = v;
                op->walk_index++;
            }
            if (op->walk_cluster != 0) {
                if (!cluster_valid(vol, op->walk_cluster)) return finish(op, RTFAT_ECORRUPT);
                op->file->cache_index = op->walk_index;
                op->file->cache_cluster = op->walk_cluster;
            }
            op->state = op->next_state;
            return RT_CONT;

        /* ---- allocation: one cluster, linked after alloc_link (0: the file's first) ---- */
        case S_ALLOC_SCAN:
            if (op->alloc_tried >= vol->cluster_count) return finish(op, RTFAT_ENOSPC);
            if (!cluster_valid(vol, op->alloc_scan)) op->alloc_scan = 2;
            if (!fat_get(vol, op, op->alloc_scan, &v)) return RT_IO;
            if (v == 0) {
                op->alloc_cluster = op->alloc_scan;
                if (op->dir_grow) op->dir_txn = 1;
                fat_set_cached(op, op->alloc_cluster, FAT_EOC);
                op->alloc_copy = 0;
                op->state = S_ALLOC_MARK_WRITE;
                return RT_CONT;
            }
            op->alloc_scan++;
            op->alloc_tried++;
            return RT_CONT;
        case S_ALLOC_MARK_WRITE:
            op->state = S_ALLOC_MARK_WRITTEN;
            return fat_issue_write(vol, op, op->alloc_copy);
        case S_ALLOC_MARK_WRITTEN:
            if (++op->alloc_copy < vol->fat_count) {
                op->state = S_ALLOC_MARK_WRITE;
                return RT_CONT;
            }
            vol->alloc_hint = op->alloc_cluster + 1;
            if (op->dir_grow) {
                /* A directory cluster is zeroed before it is linked, so
                 * a failure between the two leaves a lost cluster rather
                 * than a directory tail full of garbage entries. */
                op->scan_sector = 0;
                zero_bytes(op->sector, RTFAT_SECTOR_BYTES);
                op->state = S_DIR_ZERO;
                return RT_CONT;
            }
            if (op->alloc_link != 0) {
                op->state = S_ALLOC_LINK;
                return RT_CONT;
            }
            op->file->first_cluster = op->alloc_cluster;
            op->file->cache_index = 0;
            op->file->cache_cluster = op->alloc_cluster;
            op->entry_dirty = 1;
            op->state = op->next_state;
            return RT_CONT;
        case S_ALLOC_LINK:
            if (!fat_get(vol, op, op->alloc_link, &v)) return RT_IO;
            fat_set_cached(op, op->alloc_link, op->alloc_cluster);
            op->alloc_copy = 0;
            op->state = S_ALLOC_LINK_WRITE;
            return RT_CONT;
        case S_ALLOC_LINK_WRITE:
            op->state = S_ALLOC_LINK_WRITTEN;
            return fat_issue_write(vol, op, op->alloc_copy);
        case S_ALLOC_LINK_WRITTEN:
            if (++op->alloc_copy < vol->fat_count) {
                op->state = S_ALLOC_LINK_WRITE;
                return RT_CONT;
            }
            if (op->file != 0) {
                op->file->cache_index = op->walk_index + 1;
                op->file->cache_cluster = op->alloc_cluster;
            }
            op->state = op->next_state;
            return RT_CONT;

        /* ---- the directory's next sector or cluster, for entries that found no room ---- */
        case S_DIR_GAP_WRITTEN:
            if (op->scan_sector + 1 < vol->sectors_per_cluster) {
                op->free_lba = op->scan_lba + 1;
                op->free_index = 0;
                op->entry_zero = 1;
                op->state = op->grow_next;
                return RT_CONT;
            }
            op->state = S_DIR_NEXT;
            return RT_CONT;
        case S_DIR_NEXT:
            if (!fat_get(vol, op, op->scan_cluster, &v)) return RT_IO;
            if (v < 0x0FFFFFF8u) {
                /* Chained after the end-of-directory entry: unused. */
                if (!cluster_valid(vol, v)) return finish(op, RTFAT_ECORRUPT);
                op->free_lba = rtfat_cluster_lba(vol, v);
                op->free_index = 0;
                op->entry_zero = 1;
                op->state = op->grow_next;
                return RT_CONT;
            }
            /* A new cluster: marked in the FAT, zeroed (S_DIR_ZERO),
             * then linked after the last one. */
            op->alloc_link = op->scan_cluster;
            op->alloc_scan = vol->alloc_hint;
            op->alloc_tried = 0;
            op->dir_grow = 1;
            op->next_state = S_DIR_GROWN;
            op->state = S_ALLOC_SCAN;
            return RT_CONT;
        case S_DIR_ZERO:
            /* Every sector of the new cluster zeroed, one at a time (the
             * scratch sector is the only buffer every operation has). */
            if (op->scan_sector < vol->sectors_per_cluster) {
                const uint32_t lba = rtfat_cluster_lba(vol, op->alloc_cluster) + op->scan_sector;
                op->scan_sector++;
                return issue(op, lba, 1, 1, op->sector);
            }
            op->state = S_ALLOC_LINK;
            return RT_CONT;
        case S_DIR_GROWN:
            op->dir_grow = 0;
            op->free_lba = rtfat_cluster_lba(vol, op->alloc_cluster);
            op->free_index = 0;
            op->entry_zero = 0; /* zeroed on the card before it was linked */
            op->state = op->grow_next;
            return RT_CONT;

        /* ---- writing new entries at (free_lba, free_index) ---- */
        case S_ENTRY_READ:
            op->state = S_ENTRY_FILL;
            return issue(op, op->free_lba, 1, 0, op->sector);
        case S_ENTRY_FILL:
            if (op->entry_zero) zero_bytes(op->sector, RTFAT_SECTOR_BYTES);
            fill_entries(op);
            op->state = S_ENTRY_WRITTEN;
            return issue(op, op->free_lba, 1, 1, op->sector);
        case S_ENTRY_WRITTEN:
            /* The first entry makes this extension durable.  A later
             * rename-source delete may fail, but must not detach the new
             * directory cluster that now contains a valid entry. */
            op->dir_txn = 0;
            op->state = op->next_state;
            return RT_CONT;

        /* ---- failed directory extension recovery ---- */
        case S_DIR_RECOVER_PARENT_READ:
            op->state = S_DIR_RECOVER_PARENT_READ_DONE;
            return fat_issue_copy_read(vol, op, op->recovery_copy, op->alloc_link);
        case S_DIR_RECOVER_PARENT_READ_DONE:
            if (op->io_status != 0) {
                op->recovery_failed = 1;
                if (++op->recovery_copy < vol->fat_count) {
                    op->state = S_DIR_RECOVER_PARENT_READ;
                    return RT_CONT;
                }
                op->recovery_copy = 0;
                op->state = S_DIR_RECOVER_CANDIDATE_READ;
                return RT_CONT;
            }
            fat_set_cached(op, op->alloc_link, FAT_EOC);
            op->state = S_DIR_RECOVER_PARENT_WRITE_DONE;
            return fat_issue_copy_write(vol, op, op->recovery_copy, op->alloc_link);
        case S_DIR_RECOVER_PARENT_WRITE_DONE:
            if (op->io_status != 0) op->recovery_failed = 1;
            if (++op->recovery_copy < vol->fat_count) {
                op->state = S_DIR_RECOVER_PARENT_READ;
                return RT_CONT;
            }
            op->recovery_copy = 0;
            op->state = S_DIR_RECOVER_CANDIDATE_READ;
            return RT_CONT;
        case S_DIR_RECOVER_CANDIDATE_READ:
            op->state = S_DIR_RECOVER_CANDIDATE_READ_DONE;
            return fat_issue_copy_read(vol, op, op->recovery_copy, op->alloc_cluster);
        case S_DIR_RECOVER_CANDIDATE_READ_DONE:
            if (op->io_status != 0) {
                op->recovery_failed = 1;
                if (++op->recovery_copy < vol->fat_count) {
                    op->state = S_DIR_RECOVER_CANDIDATE_READ;
                    return RT_CONT;
                }
                op->state = S_DIR_RECOVER_DONE;
                return RT_CONT;
            }
            fat_set_cached(op, op->alloc_cluster, 0);
            op->state = S_DIR_RECOVER_CANDIDATE_WRITE_DONE;
            return fat_issue_copy_write(vol, op, op->recovery_copy, op->alloc_cluster);
        case S_DIR_RECOVER_CANDIDATE_WRITE_DONE:
            if (op->io_status != 0) op->recovery_failed = 1;
            if (++op->recovery_copy < vol->fat_count) {
                op->state = S_DIR_RECOVER_CANDIDATE_READ;
                return RT_CONT;
            }
            op->state = S_DIR_RECOVER_DONE;
            return RT_CONT;
        case S_DIR_RECOVER_DONE:
            op->dir_txn = 0;
            if (op->recovery_failed) vol->mutation_uncertain = 1;
            return finish(op, RTFAT_EIO);

        /* ---- marking op->source's entries deleted ---- */
        case S_MARK_READ1:
            op->state = S_MARK_FILL1;
            return issue(op, op->source.lfn_lba, 1, 0, op->sector);
        case S_MARK_FILL1: {
            uint32_t i = op->source.lfn_index;
            uint32_t last = op->source.lfn_lba == op->source.entry_lba ? op->source.entry_index
                                                                        : ENTRIES_PER_SECTOR - 1;
            for (; i <= last; ++i) op->sector[i * ENTRY_BYTES] = 0xE5;
            op->state = op->source.lfn_lba == op->source.entry_lba ? S_MARK_DONE : S_MARK_READ2;
            return issue(op, op->source.lfn_lba, 1, 1, op->sector);
        }
        case S_MARK_READ2:
            op->state = S_MARK_FILL2;
            return issue(op, op->source.entry_lba, 1, 0, op->sector);
        case S_MARK_FILL2: {
            uint32_t i;
            for (i = 0; i <= op->source.entry_index; ++i) op->sector[i * ENTRY_BYTES] = 0xE5;
            op->state = S_MARK_DONE;
            return issue(op, op->source.entry_lba, 1, 1, op->sector);
        }
        case S_MARK_DONE:
            op->state = op->next_state;
            return RT_CONT;

        /* ---- releasing the chain from free_cluster ---- */
        case S_FREE_LOOP:
            if (op->free_cluster == 0 || !cluster_valid(vol, op->free_cluster) ||
                ++op->scan_clusters > vol->cluster_count) {
                op->state = op->next_state;
                return RT_CONT;
            }
            if (!fat_get(vol, op, op->free_cluster, &v)) return RT_IO;
            op->free_next = v >= 0x0FFFFFF8u ? 0 : v;
            fat_set_cached(op, op->free_cluster, 0);
            if (op->free_cluster < vol->alloc_hint) vol->alloc_hint = op->free_cluster;
            op->alloc_copy = 0;
            op->state = S_FREE_WRITE;
            return RT_CONT;
        case S_FREE_WRITE:
            op->state = S_FREE_WRITTEN;
            return fat_issue_write(vol, op, op->alloc_copy);
        case S_FREE_WRITTEN:
            if (++op->alloc_copy < vol->fat_count) {
                op->state = S_FREE_WRITE;
                return RT_CONT;
            }
            op->free_cluster = op->free_next;
            op->state = S_FREE_LOOP;
            return RT_CONT;

        /* ---- LOOKUP ---- */
        case S_LOOKUP_DONE:
            return finish(op, RTFAT_OK);

        /* ---- READ ---- */
        case S_READ_NEXT:
            if (op->data_done == op->want) {
                op->file->position += op->want;
                return finish(op, (int32_t)op->want);
            }
            walk_begin(op, (op->file->position + op->data_done) / rtfat_cluster_bytes(vol), S_READ_CHUNK);
            return RT_CONT;
        case S_READ_CHUNK:
            if (op->walk_cluster == 0) return finish(op, RTFAT_ECORRUPT); /* size promises more than the chain has */
            data_chunk(vol, op, op->file->position + op->data_done, op->want - op->data_done);
            op->state = S_READ_COPY;
            return issue(op, op->chunk_lba, op->chunk_count, 0, (void*)(uintptr_t)op->bounce);
        case S_READ_COPY:
            copy_bytes((uint8_t*)(uintptr_t)op->buffer + op->data_done,
                       (const uint8_t*)(uintptr_t)op->bounce + op->chunk_skip, op->chunk_bytes);
            op->data_done += op->chunk_bytes;
            op->state = S_READ_NEXT;
            return RT_CONT;

        /* ---- WRITE ---- */
        case S_WRITE_NEXT:
            if (op->data_done == op->length) {
                op->state = S_WRITE_FINISH;
                return RT_CONT;
            }
            walk_begin(op, (op->file->position + op->data_done) / rtfat_cluster_bytes(vol), S_WRITE_CHUNK);
            return RT_CONT;
        case S_WRITE_CHUNK: {
            const uint32_t abs = op->file->position + op->data_done;
            if (op->walk_cluster == 0) {
                /* The chain ends before this position: one more cluster. */
                op->alloc_link = op->walk_prev;
                op->alloc_scan = vol->alloc_hint;
                op->alloc_tried = 0;
                op->next_state = S_WRITE_NEXT;
                op->state = S_ALLOC_SCAN;
                return RT_CONT;
            }
            data_chunk(vol, op, abs, op->length - op->data_done);
            if ((op->chunk_skip != 0 || (op->chunk_skip + op->chunk_bytes) % RTFAT_SECTOR_BYTES != 0) &&
                abs - op->chunk_skip < op->grow_from) {
                /* Partial sectors over existing content: read them first. */
                op->state = S_WRITE_OVERLAY;
                return issue(op, op->chunk_lba, op->chunk_count, 0, (void*)(uintptr_t)op->bounce);
            }
            zero_bytes((uint8_t*)(uintptr_t)op->bounce, op->chunk_count * RTFAT_SECTOR_BYTES);
            copy_bytes((uint8_t*)(uintptr_t)op->bounce + op->chunk_skip,
                       (const uint8_t*)(uintptr_t)op->buffer + op->data_done, op->chunk_bytes);
            op->state = S_WRITE_WRITTEN;
            return issue(op, op->chunk_lba, op->chunk_count, 1, (void*)(uintptr_t)op->bounce);
        }
        case S_WRITE_OVERLAY:
            copy_bytes((uint8_t*)(uintptr_t)op->bounce + op->chunk_skip,
                       (const uint8_t*)(uintptr_t)op->buffer + op->data_done, op->chunk_bytes);
            op->state = S_WRITE_WRITTEN;
            return issue(op, op->chunk_lba, op->chunk_count, 1, (void*)(uintptr_t)op->bounce);
        case S_WRITE_WRITTEN:
            op->data_done += op->chunk_bytes;
            if (op->file->position + op->data_done > op->file->size) {
                op->file->size = op->file->position + op->data_done;
                op->entry_dirty = 1;
            }
            op->state = S_WRITE_NEXT;
            return RT_CONT;
        case S_WRITE_FINISH:
            op->file->position += op->length;
            if (!op->entry_dirty) return finish(op, (int32_t)op->length);
            op->state = S_WRITE_ENTRY_READ;
            return RT_CONT;
        case S_WRITE_ENTRY_READ:
            op->state = S_WRITE_ENTRY_UPDATE;
            return issue(op, op->file->entry_lba, 1, 0, op->sector);
        case S_WRITE_ENTRY_UPDATE: {
            uint8_t* e = op->sector + op->file->entry_index * ENTRY_BYTES;
            if (e[0] == 0x00 || e[0] == 0xE5 || (e[11] & 0x3Fu) == ATTR_LFN) return finish(op, RTFAT_ECORRUPT);
            entry_set_cluster(e, op->file->first_cluster);
            wr32(e + 0x1C, op->file->size);
            op->state = S_WRITE_ENTRY_WRITTEN;
            return issue(op, op->file->entry_lba, 1, 1, op->sector);
        }
        case S_WRITE_ENTRY_WRITTEN:
            return finish(op, (int32_t)op->length);

        /* ---- CREATE ---- */
        case S_CREATE_SCANNED:
            op->next_state = S_CREATE_WRITTEN;
            op->state = S_ENTRY_READ;
            return RT_CONT;
        case S_CREATE_WRITTEN:
            return finish(op, RTFAT_OK);

        /* ---- DELETE ---- */
        case S_DELETE_FOUND:
            op->source = op->found;
            op->next_state = S_DELETE_MARKED;
            op->state = S_MARK_READ1;
            return RT_CONT;
        case S_DELETE_MARKED:
            op->free_cluster = op->source.first_cluster;
            op->scan_clusters = 0;
            op->next_state = S_LOOKUP_DONE; /* finish OK */
            op->state = S_FREE_LOOP;
            return RT_CONT;

        /* ---- RENAME ---- */
        case S_RENAME_FOUND:
            op->source = op->found;
            if (!prepare_new_entry(op, op->name2, op->source.first_cluster, op->source.size,
                                   hidden_attribute(op, op->source.attributes))) {
                return finish(op, RTFAT_EINVAL);
            }
            scan_begin(vol, op, SCAN_CREATE, S_RENAME_SCANNED);
            return RT_CONT;
        case S_RENAME_SCANNED:
            op->next_state = S_RENAME_WRITTEN;
            op->state = S_ENTRY_READ;
            return RT_CONT;
        case S_RENAME_WRITTEN:
            op->next_state = S_RENAME_MARKED;
            op->state = S_MARK_READ1;
            return RT_CONT;
        case S_RENAME_MARKED:
            return finish(op, RTFAT_OK);

        /* ---- LIST / COUNT / USAGE ---- */
        case S_LIST_DONE:
            if (op->kind == RTFAT_OP_LIST && op->count > op->length) op->count = op->length;
            return finish(op, (int32_t)op->count);

        default:
            return finish(op, RTFAT_EINVAL);
    }
}

void rtfat_begin(struct rtfat_op* op, uint32_t kind) {
    /* Everything after the parameters is scratch; clear it field by field
     * (no memset here: the runtime links against nothing). */
    uint8_t* p = (uint8_t*)&op->found;
    const uint32_t n = (uint32_t)((uint8_t*)op->fat_sector - p);
    zero_bytes(p, n);
    op->kind = kind;
    op->state = S_IDLE;
    op->result = 0;
    op->io_lba = 0;
    op->io_count = 0;
    op->io_write = 0;
    op->io_buffer = 0;
    op->io_status = 0;
    op->io_pending = 0;
    op->io_transfers = 0;
}

int rtfat_step(struct rtfat_volume* vol, struct rtfat_op* op) {
    int r;
    if (op->io_pending) {
        op->io_pending = 0;
        op->io_transfers++;
        if (op->io_status != 0) {
            op->fat_cached_lba = 0;
            if (recovery_state(op->state)) {
                /* The recovery states consume the failure and continue
                 * restoring the other FAT copies. */
            } else if (op->dir_txn) {
                begin_dir_recovery(vol, op);
            } else if (ordinary_fat_mutation_write_state(op->state)) {
                vol->mutation_uncertain = 1;
                finish(op, RTFAT_EIO);
                return RTFAT_DONE;
            } else {
                finish(op, RTFAT_EIO);
                return RTFAT_DONE;
            }
        }
    }
    if (op->state == S_IDLE) {
        if (vol->mutation_uncertain &&
            (op->kind == RTFAT_OP_WRITE || op->kind == RTFAT_OP_CREATE ||
             op->kind == RTFAT_OP_DELETE || op->kind == RTFAT_OP_RENAME)) {
            return done_now(op, RTFAT_EIO);
        }
        /* First step: validate the parameters and enter the operation. */
        switch (op->kind) {
            case RTFAT_OP_LOOKUP:
                if (!rtfat_valid_name(op->name)) return done_now(op, RTFAT_EINVAL);
                op->new_name = op->name;
                scan_begin(vol, op, SCAN_FIND, S_LOOKUP_DONE);
                break;
            case RTFAT_OP_READ:
                if (op->file == 0 || op->bounce == 0 || op->bounce_bytes < RTFAT_SECTOR_BYTES) {
                    return done_now(op, RTFAT_EINVAL);
                }
                if (op->length == 0 || op->file->position >= op->file->size) return done_now(op, 0);
                op->want = op->file->size - op->file->position;
                if (op->want > op->length) op->want = op->length;
                op->data_done = 0;
                op->state = S_READ_NEXT;
                break;
            case RTFAT_OP_WRITE:
                if (op->file == 0 || op->bounce == 0 || op->bounce_bytes < RTFAT_SECTOR_BYTES) {
                    return done_now(op, RTFAT_EINVAL);
                }
                if (op->file->position > op->file->size) return done_now(op, RTFAT_EINVAL);
                if (op->length == 0) return done_now(op, 0);
                if (op->file->position + op->length < op->file->position) return done_now(op, RTFAT_ENOSPC);
                op->grow_from = op->file->size;
                op->data_done = 0;
                op->entry_dirty = 0;
                op->state = S_WRITE_NEXT;
                break;
            case RTFAT_OP_CREATE:
                if (!prepare_new_entry(op, op->name, 0, 0,
                                       ATTR_ARCHIVE | (op->want_hidden == 1u ? ATTR_HIDDEN : 0u))) {
                    return done_now(op, RTFAT_EINVAL);
                }
                scan_begin(vol, op, SCAN_CREATE, S_CREATE_SCANNED);
                break;
            case RTFAT_OP_DELETE:
                if (!rtfat_valid_name(op->name)) return done_now(op, RTFAT_EINVAL);
                op->new_name = op->name;
                scan_begin(vol, op, SCAN_FIND, S_DELETE_FOUND);
                break;
            case RTFAT_OP_RENAME:
                if (!rtfat_valid_name(op->name) || !rtfat_valid_name(op->name2)) {
                    return done_now(op, RTFAT_EINVAL);
                }
                if (names_equal(op->name, op->name2)) return done_now(op, RTFAT_EEXIST);
                op->new_name = op->name;
                scan_begin(vol, op, SCAN_FIND, S_RENAME_FOUND);
                break;
            case RTFAT_OP_LIST:
                if (op->buffer == 0 && op->length != 0) return done_now(op, RTFAT_EINVAL);
                scan_begin(vol, op, SCAN_LIST, S_LIST_DONE);
                break;
            case RTFAT_OP_COUNT:
            case RTFAT_OP_USAGE:
                scan_begin(vol, op, SCAN_LIST, S_LIST_DONE);
                break;
            default:
                return done_now(op, RTFAT_EINVAL);
        }
    }
    if (op->state == S_DONE) return RTFAT_DONE;
    do {
        r = run_state(vol, op);
    } while (r == RT_CONT && op->state != S_DONE);
    return r == RT_IO ? RTFAT_IO : RTFAT_DONE;
}
