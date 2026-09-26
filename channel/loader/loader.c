// SPDX-License-Identifier: GPL-3.0-or-later
//
// The RiftWii channel's boot program: a few KiB, so the Wii and the vWii
// load it alike (see start.S). It reads the forwarder (channel/forwarder,
// the channel's content 2) through ES into MEM2, copies its sections into
// place and starts it. With nothing to start it goes back to the Wii Menu.
//
// It runs before anything else, with no library: IOS is reached by hand
// through the IPC registers, polled, the way libogc's ipc.c drives them.

#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;

#define FORWARDER_CONTENT 2
#define DOL_BUFFER ((u8*)0x91000000)  // MEM2, clear of what an IOS reload uses
#define DOL_MAX (8u << 20)
#define MEM1_HI 0x81700000u
#define CHUNK 0x40000u

extern u8 __loader_end[];

// ---------------------------------------------------------------- memory ----
void* memset(void* dst, int value, unsigned long n) {
    u8* d = dst;
    while (n--) *d++ = (u8)value;
    return dst;
}

void* memcpy(void* dst, const void* src, unsigned long n) {
    u8* d = dst;
    const u8* s = src;
    while (n--) *d++ = *s++;
    return dst;
}

static u32 phys(const void* p) { return (u32)p & 0x3fffffffu; }

static void dc_flush(const void* p, u32 n) {
    for (u32 a = (u32)p & ~31u; a < (u32)p + n; a += 32) __asm__ volatile("dcbf 0,%0" ::"r"(a) : "memory");
    __asm__ volatile("sync" ::: "memory");
}

static void dc_invalidate(const void* p, u32 n) {
    for (u32 a = (u32)p & ~31u; a < (u32)p + n; a += 32) __asm__ volatile("dcbi 0,%0" ::"r"(a) : "memory");
    __asm__ volatile("sync" ::: "memory");
}

static void ic_invalidate(const void* p, u32 n) {
    for (u32 a = (u32)p & ~31u; a < (u32)p + n; a += 32) __asm__ volatile("icbi 0,%0" ::"r"(a) : "memory");
    __asm__ volatile("sync; isync" ::: "memory");
}

// ------------------------------------------------------------------- IPC ----
// Hollywood's IPC registers, uncached (DBAT1): the request's physical
// address, the control bits, IOS's reply.
#define IPC_PPCMSG (*(volatile u32*)0xcd000000)
#define IPC_PPCCTRL (*(volatile u32*)0xcd000004)
#define IPC_ARMMSG (*(volatile u32*)0xcd000008)
#define HW_PPCIRQFLAG (*(volatile u32*)0xcd000030)
#define CTRL_SEND 0x01
#define CTRL_ACK 0x02    // IOS took the request
#define CTRL_REPLY 0x04  // IOS answered one
#define CTRL_DONE 0x08   // the answer is read
#define CTRL_KEEP 0x30   // the interrupt enables, left as they are

enum { IOS_OPEN = 1, IOS_CLOSE = 2, IOS_IOCTLV = 7 };

typedef struct {
    u32 cmd;
    s32 result;
    s32 fd;
    u32 arg[5];
} __attribute__((aligned(64))) Request;

typedef struct {
    u32 data;  // physical
    u32 len;
} Vector;

static Request g_req;
static Vector g_vec[4] __attribute__((aligned(32)));

static void ctrl(u32 bit) { IPC_PPCCTRL = (IPC_PPCCTRL & CTRL_KEEP) | bit; }

static void ipc_send(void) {
    dc_flush(&g_req, sizeof(g_req));
    // A leftover ack or answer (IOS raises one when it starts) goes first.
    if (IPC_PPCCTRL & CTRL_ACK) ctrl(CTRL_ACK);
    IPC_PPCMSG = phys(&g_req);
    ctrl(CTRL_SEND);
    while (!(IPC_PPCCTRL & CTRL_ACK)) {
    }
    ctrl(CTRL_ACK);
    HW_PPCIRQFLAG = 0x40000000;
}

static s32 ipc(void) {
    ipc_send();
    for (;;) {
        while (!(IPC_PPCCTRL & CTRL_REPLY)) {
        }
        const u32 reply = IPC_ARMMSG;
        ctrl(CTRL_REPLY);
        HW_PPCIRQFLAG = 0x40000000;
        ctrl(CTRL_DONE);
        if (reply == phys(&g_req)) break;
    }
    dc_invalidate(&g_req, sizeof(g_req));
    return g_req.result;
}

static s32 ios_open(const char* path) {
    dc_flush(path, 32);
    memset(&g_req, 0, sizeof(g_req));
    g_req.cmd = IOS_OPEN;
    g_req.arg[0] = phys(path);
    return ipc();
}

static void ios_close(s32 fd) {
    memset(&g_req, 0, sizeof(g_req));
    g_req.cmd = IOS_CLOSE;
    g_req.fd = fd;
    ipc();
}

// `in` vectors then `io` ones; the caller fills g_vec with virtual
// addresses, flushed here, and the `io` buffers invalidated after.
static s32 ios_ioctlv(s32 fd, u32 ioctl, u32 in, u32 io, int wait) {
    Vector v[4];
    for (u32 i = 0; i < in + io; ++i) {
        v[i] = g_vec[i];
        dc_flush((const void*)g_vec[i].data, g_vec[i].len);
        g_vec[i].data = phys((const void*)g_vec[i].data);
    }
    dc_flush(g_vec, sizeof(g_vec));
    memset(&g_req, 0, sizeof(g_req));
    g_req.cmd = IOS_IOCTLV;
    g_req.fd = fd;
    g_req.arg[0] = ioctl;
    g_req.arg[1] = in;
    g_req.arg[2] = io;
    g_req.arg[3] = phys(g_vec);
    if (!wait) {
        ipc_send();
        return 0;
    }
    const s32 r = ipc();
    for (u32 i = in; i < in + io; ++i) dc_invalidate((const void*)v[i].data, v[i].len);
    return r;
}

static void vec(u32 i, const void* data, u32 len) {
    g_vec[i].data = (u32)data;
    g_vec[i].len = len;
}

// -------------------------------------------------------------------- ES ----
enum {
    ES_LAUNCH = 0x08,
    ES_OPEN_CONTENT = 0x09,
    ES_READ_CONTENT = 0x0a,
    ES_CLOSE_CONTENT = 0x0b,
    ES_GET_VIEW_COUNT = 0x12,
    ES_GET_VIEWS = 0x13,
    ES_SEEK_CONTENT = 0x23,
};

static const char g_es_path[32] __attribute__((aligned(32))) = "/dev/es";
static s32 g_es = -1;
static u32 g_arg[4] __attribute__((aligned(32)));
static u64 g_title __attribute__((aligned(32)));
static u8 g_view[0xd8] __attribute__((aligned(32)));

// The forwarder's DOL, read whole into MEM2. Its size, or 0.
static u32 read_forwarder(void) {
    g_arg[0] = FORWARDER_CONTENT;
    vec(0, &g_arg[0], 4);
    const s32 cfd = ios_ioctlv(g_es, ES_OPEN_CONTENT, 1, 0, 1);
    if (cfd < 0) return 0;
    g_arg[0] = (u32)cfd;
    g_arg[1] = 0;
    g_arg[2] = 2;  // from the end: the size
    vec(0, &g_arg[0], 4);
    vec(1, &g_arg[1], 4);
    vec(2, &g_arg[2], 4);
    const s32 size = ios_ioctlv(g_es, ES_SEEK_CONTENT, 3, 0, 1);
    u32 got = 0;
    if (size > 0x100 && (u32)size <= DOL_MAX) {
        g_arg[0] = (u32)cfd;
        g_arg[1] = 0;
        g_arg[2] = 0;  // back to the start
        vec(0, &g_arg[0], 4);
        vec(1, &g_arg[1], 4);
        vec(2, &g_arg[2], 4);
        if (ios_ioctlv(g_es, ES_SEEK_CONTENT, 3, 0, 1) == 0) {
            while (got < (u32)size) {
                u32 n = (u32)size - got;
                if (n > CHUNK) n = CHUNK;
                g_arg[0] = (u32)cfd;
                vec(0, &g_arg[0], 4);
                vec(1, DOL_BUFFER + got, n);
                const s32 r = ios_ioctlv(g_es, ES_READ_CONTENT, 1, 1, 1);
                if (r <= 0) break;
                got += (u32)r;
            }
        }
    }
    g_arg[0] = (u32)cfd;
    vec(0, &g_arg[0], 4);
    ios_ioctlv(g_es, ES_CLOSE_CONTENT, 1, 0, 1);
    return size > 0 && got == (u32)size ? got : 0;
}

// Back to the Wii Menu (00000001-00000002). IOS reloads: no answer.
static void wii_menu(void) {
    g_title = 0x0000000100000002ull;
    vec(0, &g_title, 8);
    vec(1, &g_arg[0], 4);
    if (ios_ioctlv(g_es, ES_GET_VIEW_COUNT, 1, 1, 1) >= 0 && g_arg[0] >= 1) {
        g_arg[0] = 1;
        vec(0, &g_title, 8);
        vec(1, &g_arg[0], 4);
        vec(2, g_view, sizeof(g_view));
        if (ios_ioctlv(g_es, ES_GET_VIEWS, 2, 1, 1) >= 0) {
            vec(0, &g_title, 8);
            vec(1, g_view, sizeof(g_view));
            ios_ioctlv(g_es, ES_LAUNCH, 2, 0, 0);
        }
    }
    for (;;) {
    }
}

// ------------------------------------------------------------------- DOL ----
typedef struct {
    u32 offset[18];
    u32 address[18];
    u32 size[18];
    u32 bss_address;
    u32 bss_size;
    u32 entry;
} DolHeader;

// Every section in MEM1 above this loader, and inside the file.
static int dol_valid(const DolHeader* h, u32 size) {
    const u32 self_hi = (u32)__loader_end;
    for (int i = 0; i < 18; ++i) {
        if (!h->size[i]) continue;
        const u32 lo = h->address[i], hi = lo + h->size[i];
        if (h->offset[i] > size || h->size[i] > size - h->offset[i]) return 0;
        if (lo < self_hi || hi > MEM1_HI || hi < lo) return 0;
    }
    const u32 lo = h->bss_address, hi = lo + h->bss_size;
    if (h->bss_size && (lo < self_hi || hi > MEM1_HI || hi < lo)) return 0;
    return h->entry >= self_hi && h->entry < MEM1_HI;
}

typedef void (*Entry)(void);

void loader_main(void) {
    g_es = ios_open(g_es_path);
    if (g_es < 0) {
        for (;;) {
        }
    }
    const u32 size = read_forwarder();
    const DolHeader* h = (const DolHeader*)DOL_BUFFER;
    if (!size || !dol_valid(h, size)) wii_menu();
    ios_close(g_es);

    if (h->bss_size) {
        memset((void*)h->bss_address, 0, h->bss_size);
        dc_flush((void*)h->bss_address, h->bss_size);
    }
    for (int i = 0; i < 18; ++i) {
        if (!h->size[i]) continue;
        void* at = (void*)h->address[i];
        memcpy(at, DOL_BUFFER + h->offset[i], h->size[i]);
        dc_flush(at, h->size[i]);
        ic_invalidate(at, h->size[i]);
    }
    ((Entry)h->entry)();
}
