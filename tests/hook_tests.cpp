// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/fst.hpp"
#include "riftwii/hook.hpp"
#include "riftwii/symsearch.hpp"
#include "rt_hook.h"
#include "rtable.h"
#include "fat32_image.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using Bytes = std::vector<std::uint8_t>;

// The runtime addresses memory with 32-bit fields (it is a 32-bit target),
// so driving it on a 64-bit host needs buffers below 4 GiB.
#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
static std::uint8_t* LowBuffer(std::size_t bytes) {
    for (std::uintptr_t hint = 0x10000000; hint < 0x70000000; hint += 0x10000000) {
        void* p = VirtualAlloc(reinterpret_cast<void*>(hint), bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p) return static_cast<std::uint8_t*>(p);
    }
    return nullptr;
}
#elif defined(__linux__)
#include <sys/mman.h>
static std::uint8_t* LowBuffer(std::size_t bytes) {
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    return p == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(p);
}
#else
static std::uint8_t* LowBuffer(std::size_t) { return nullptr; }
#endif

static void Put32(Bytes& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}

// ---- blob header ----------------------------------------------------------

static Bytes MakeBlob() {
    Bytes b(0x1000, 0);
    Put32(b, 0, RT_BLOB_MAGIC);
    Put32(b, 4, RT_BLOB_VERSION);
    Put32(b, 8, 0x1000);
    Put32(b, 12, 0x800);  // context
    for (std::uint32_t i = 0; i < RT_IPC_ENTRIES; ++i) {
        Put32(b, 16 + i * 4, 0x200 + i * 4);
        Put32(b, 16 + RT_IPC_ENTRIES * 4 + i * 4, 0x300 + i * 32);
        Put32(b, 16 + RT_IPC_ENTRIES * 8 + i * 4, 0x310 + i * 32);
    }
    Put32(b, 16 + RT_IPC_ENTRIES * 12, 0x600);  // completion entry
    Put32(b, 0x800, RT_CONTEXT_MAGIC);
    return b;
}

static void TestBlob() {
    riftwii::ResidentBlob rb;
    std::string error;
    Bytes b = MakeBlob();
    EXPECT_TRUE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    EXPECT_EQ(rb.size, 0x1000u);
    EXPECT_EQ(rb.context_offset, 0x800u);
    EXPECT_EQ(rb.hook_offsets[0], 0x200u);
    EXPECT_EQ(rb.hook_ioctl_async_offset, 0x214u);
    EXPECT_EQ(rb.replay_ioctl_async_offset, 0x3A0u);
    EXPECT_EQ(rb.continue_ioctl_async_offset, 0x3B0u);
    EXPECT_EQ(rb.complete_di_offset, 0x600u);
    EXPECT_EQ(RT_IPC_ASYNC(1), 0u);
    EXPECT_EQ(RT_IPC_ASYNC(7), 6u);
    EXPECT_EQ(RT_IPC_SYNC(1), 7u);
    EXPECT_EQ(RT_IPC_SYNC(7), 13u);
    EXPECT_EQ(RT_IPC_ASYNC_IOCTL, 5u);

    b = MakeBlob();
    Put32(b, 0, 0x12345678);
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 4, 99);
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 8, 0xFE0);  // size field disagrees with the data
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 12, 0x900);  // context would run past the end
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 16 + RT_IPC_ENTRIES * 8, 0x314);  // continue slot must follow replay
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 16 + RT_IPC_ENTRIES * 12, 0xFFE);  // completion entry past the end
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 0x800, 0);  // context magic
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 16, 0xFFE);  // one bad hook
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 16 + RT_IPC_ENTRIES * 4 + 4, 0x304);  // one unaligned replay
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 16 + RT_IPC_ENTRIES * 4 + 13 * 4, 0xFF0);  // last replay runs past blob
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 16 + RT_IPC_ENTRIES * 4 + 2 * 4, 0x300);  // duplicate replay/continue region
    Put32(b, 16 + RT_IPC_ENTRIES * 8 + 2 * 4, 0x310);
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 16 + RT_IPC_ENTRIES * 4 + 2 * 4, 0x310);  // overlaps entry 0's continuation half
    Put32(b, 16 + RT_IPC_ENTRIES * 8 + 2 * 4, 0x320);
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 16 + RT_IPC_ENTRIES * 4 + 2 * 4, 0x800);  // writable pair cannot overlap context
    Put32(b, 16 + RT_IPC_ENTRIES * 8 + 2 * 4, 0x810);
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), 16, rb, error));
}

// ---- instruction helpers --------------------------------------------------

static void TestJumpAndDisplace() {
    const auto j = riftwii::encode_absolute_jump(0, 0x935D0020);
    EXPECT_EQ(j[0], 0x3C00935Du);  // lis r0, 0x935d
    EXPECT_EQ(j[1], 0x60000020u);  // ori r0, r0, 0x20
    EXPECT_EQ(j[2], 0x7C0903A6u);  // mtctr r0
    EXPECT_EQ(j[3], 0x4E800420u);  // bctr
    const auto k = riftwii::encode_absolute_jump(12, 0x801940C8);
    EXPECT_EQ(k[0], 0x3D808019u);  // lis r12, 0x8019
    EXPECT_EQ(k[1], 0x618C40C8u);  // ori r12, r12, 0x40c8
    EXPECT_EQ(k[2], 0x7D8903A6u);  // mtctr r12

    std::string why;
    EXPECT_TRUE(riftwii::displaceable(0x9421FFC0, 12, why));   // stwu r1,-0x40(r1)
    EXPECT_TRUE(riftwii::displaceable(0x7C0802A6, 12, why));   // mflr r0
    EXPECT_TRUE(riftwii::displaceable(0x90010044, 12, why));   // stw r0,0x44(r1)
    EXPECT_TRUE(riftwii::displaceable(0x39610040, 12, why));   // addi r11,r1,0x40
    EXPECT_FALSE(riftwii::displaceable(0x4BE8C969, 12, why));  // bl
    EXPECT_FALSE(riftwii::displaceable(0x48000010, 12, why));  // b
    EXPECT_FALSE(riftwii::displaceable(0x4082000C, 12, why));  // bne
    EXPECT_FALSE(riftwii::displaceable(0x4E800020, 12, why));  // blr
    EXPECT_FALSE(riftwii::displaceable(0x4E800420, 12, why));  // bctr
    EXPECT_FALSE(riftwii::displaceable(0x44000002, 12, why));  // sc
    EXPECT_FALSE(riftwii::displaceable(0x00000000, 12, why));
    EXPECT_FALSE(riftwii::displaceable(0x7D8C6378, 12, why));  // mr r12, r12
    EXPECT_FALSE(riftwii::displaceable(0x3D800000, 12, why));  // lis r12, 0
    EXPECT_TRUE(riftwii::displaceable(0x3D600000, 12, why));   // lis r11, 0 is fine
    EXPECT_TRUE(riftwii::displaceable(0x60000000, 12, why));   // nop
    EXPECT_TRUE(riftwii::displaceable(0x60000000, 0, why));    // nop, even with r0 as the scratch
    EXPECT_FALSE(riftwii::displaceable(0x618C1234, 12, why));  // ori r12,r12,0x1234
    EXPECT_FALSE(riftwii::displaceable(0x606C0000, 12, why));  // ori r12,r3,0
    EXPECT_TRUE(riftwii::displaceable(0x60630000, 12, why));   // ori r3,r3,0
    EXPECT_TRUE(riftwii::displaceable(0x38616040, 12, why));   // addi r3,r1,0x6040: immediate, not rB
    EXPECT_TRUE(riftwii::displaceable(0x80616040, 12, why));   // lwz r3,0x6040(r1)
    EXPECT_TRUE(riftwii::displaceable(0x5464603E, 12, why));   // rlwinm r4,r3,12,0,31: SH, not rB
    EXPECT_FALSE(riftwii::displaceable(0x7C836378, 12, why));  // or r3,r4,r12
    EXPECT_FALSE(riftwii::displaceable(0x7C0903A6, 12, why));  // mtctr r0
    EXPECT_FALSE(riftwii::displaceable(0x7C6902A6, 12, why));  // mfctr r3
    EXPECT_FALSE(riftwii::displaceable(0x7C6102A6, 12, why));  // mfxer r3
    EXPECT_TRUE(riftwii::displaceable(0x7C0803A6, 12, why));   // mtlr r0
    EXPECT_TRUE(riftwii::displaceable(0x7C600026, 12, why));   // mfcr r3
}

// ---- placement ------------------------------------------------------------

static void TestPlacement() {
    riftwii::ResidentPlacement p;
    std::string error;
    // Code below the MEM1 arena top (Kirby's Epic Yarn: BI2 at 0x817E9E60),
    // no data: MEM2 untouched.
    EXPECT_TRUE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x935E0000, 7840, 0, p, error));
    EXPECT_EQ(p.code_base, 0x817E7FC0u);
    EXPECT_EQ(p.code_bytes, 7840u);
    EXPECT_EQ(p.new_arena1_hi, 0x817E7FC0u);
    EXPECT_EQ(p.data_base, 0u);
    EXPECT_EQ(p.data_bytes, 0u);
    EXPECT_EQ(p.new_arena2_end, 0x935E0000u);
    // With data: the MEM2 top in 64 KiB granules.
    EXPECT_TRUE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x935E0000, 7840, 1, p, error));
    EXPECT_EQ(p.data_base, 0x935D0000u);
    EXPECT_EQ(p.data_bytes, 0x10000u);
    EXPECT_EQ(p.new_arena2_end, 0x935D0000u);
    EXPECT_TRUE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x935E0000, 7840, 0x10001, p, error));
    EXPECT_EQ(p.data_base, 0x935C0000u);
    EXPECT_TRUE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x933E0000, 7840, 0x20000, p, error));  // IOS58-style end
    EXPECT_EQ(p.data_base, 0x933C0000u);
    EXPECT_TRUE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x935DFFE0, 7840, 32, p, error));  // unaligned end rounds down
    EXPECT_EQ(p.data_base, 0x935C0000u);
    // The code must clear the floor (loader, apploader image).
    EXPECT_TRUE(riftwii::plan_resident_placement(0x81241EA0, 0x81240000, 0x935E0000, 7840, 0, p, error));
    EXPECT_EQ(p.code_base, 0x81240000u);
    EXPECT_FALSE(riftwii::plan_resident_placement(0x81241E80, 0x81240000, 0x935E0000, 7840, 0, p, error));
    EXPECT_FALSE(riftwii::plan_resident_placement(0x817E9E70, 0x81240000, 0x935E0000, 7840, 0, p, error));  // unaligned top
    EXPECT_FALSE(riftwii::plan_resident_placement(0x00000000, 0x81240000, 0x935E0000, 7840, 0, p, error));  // no arena top
    EXPECT_FALSE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x935E0000, 7841, 0, p, error));  // odd blob
    EXPECT_FALSE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x80000000, 7840, 32, p, error));  // MEM1 as MEM2 end
    EXPECT_FALSE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x90800000, 7840, 32, p, error));  // at the floor
    EXPECT_FALSE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x935E0000, 7840, 0x03000000, p, error));  // too big
    EXPECT_TRUE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x80000000, 7840, 0, p, error));  // no data: MEM2 not looked at
}

// ---- symbol search --------------------------------------------------------

namespace {

// A tiny fake game: words appended with helpers; addresses start at 0x80004000.
struct FakeText {
    Bytes bytes;
    std::uint32_t base = 0x80004000;
    std::uint32_t here() const { return base + static_cast<std::uint32_t>(bytes.size()); }
    void word(std::uint32_t w) {
        bytes.resize(bytes.size() + 4);
        Put32(bytes, bytes.size() - 4, w);
    }
    void li(unsigned reg, std::uint32_t v) { word(0x38000000u | (reg << 21) | (v & 0xFFFF)); }
    void nop() { word(0x60000000); }
    void bl(std::uint32_t target) {
        const std::int32_t d = static_cast<std::int32_t>(target - here());
        word(0x48000001u | (static_cast<std::uint32_t>(d) & 0x03FFFFFCu));
    }
    void prologue() { word(0x9421FFC0); word(0x7C0802A6); word(0x90010044); word(0x4E800020); }
    // stwu / mflr / stw / li r0,<ipc command> / blr: the shape of IOS_Ioctl*Async.
    void ipc_function(std::uint32_t ipc) {
        word(0x9421FFC0);
        word(0x7C0802A6);
        word(0x90010044);
        li(0, ipc);
        word(0x4E800020);
    }
    riftwii::CodeRange range() const { return {base, bytes.data(), bytes.size()}; }
    // An SDK IPC API function as the search sees it: prologue, the block
    // allocated (64, 32), the command stored at its first word, the
    // block handed to `submit` with the callback register (async) or 0
    // (sync); `relaunch` != 0 stores it at 0x28 as the reboot forms do.
    std::uint32_t api_function(std::uint32_t cmd, bool async, std::uint32_t alloc, std::uint32_t submit,
                               std::uint32_t relaunch = 0) {
        const std::uint32_t start = here();
        word(0x9421FFC0);  // stwu r1,-0x40(r1)
        word(0x7C0802A6);  // mflr r0
        word(0x90010044);  // stw r0,0x44(r1)
        word(0x7CBD2B78);  // mr r29,r5 (the callback)
        li(4, 64);
        li(5, 32);
        bl(alloc);
        li(0, cmd);
        word(0x90030000);  // stw r0,0(r3)
        if (relaunch != 0) {
            li(5, relaunch);
            word(0x90A30028);  // stw r5,0x28(r3)
        }
        if (async) {
            word(0x7FA4EB78);  // mr r4,r29
        } else {
            li(4, 0);
        }
        bl(submit);
        word(0x80010044);  // lwz r0,0x44(r1)
        word(0x7C0803A6);  // mtlr r0
        word(0x38210040);  // addi r1,r1,0x40
        word(0x4E800020);  // blr
        return start;
    }
};

}  // namespace

static void TestIpcApiSearch() {
    // The helpers, then the API in the SDK's order with a helper without
    // a command between the ioctl and ioctlv pairs, a reboot form of
    // ioctlv, and an unrelated function storing 1 at its argument's first
    // word (not an API function: it uses neither helper).
    FakeText t;
    const std::uint32_t alloc = t.here();
    t.prologue();
    const std::uint32_t submit = t.here();
    t.prologue();
    std::uint32_t async_at[8] = {}, sync_at[8] = {};
    for (std::uint32_t cmd = 1; cmd <= 7; ++cmd) {
        async_at[cmd] = t.api_function(cmd, true, alloc, submit);
        if (cmd == 5) continue;  // the game never calls IOS_Seek: not linked
        sync_at[cmd] = t.api_function(cmd, false, alloc, submit);
        if (cmd == 6) t.prologue();  // the vector helper
    }
    const std::uint32_t reboot = t.api_function(7, false, alloc, submit, 1);
    (void)reboot;
    const std::uint32_t unrelated = t.here();
    t.word(0x9421FFC0);
    t.li(0, 1);
    t.word(0x90030000);
    t.word(0x4E800020);
    (void)unrelated;
    // DI call sites so find_ipc_symbols has something to vote with.
    for (std::uint32_t cmd : {0x71u, 0x70u, 0x8Au, 0x8Du}) {
        t.li(4, cmd);
        t.bl(async_at[6]);
    }
    t.li(4, 0x8B);
    t.li(5, 3);
    t.li(6, 2);
    t.bl(async_at[7]);

    riftwii::IpcSymbols s;
    riftwii::IpcApi api;
    std::string error;
    EXPECT_TRUE(riftwii::find_ipc_symbols({t.range()}, s, error));
    EXPECT_EQ(s.ioctl_async, async_at[6]);
    EXPECT_EQ(s.ioctlv_async, async_at[7]);
    EXPECT_TRUE(riftwii::find_ipc_api({t.range()}, s, api, error));
    for (std::uint32_t cmd = 1; cmd <= 7; ++cmd) {
        EXPECT_EQ(api.async[cmd], async_at[cmd]);
        EXPECT_EQ(api.sync[cmd], sync_at[cmd]);
    }
    EXPECT_EQ(api.sync[5], 0u);

    // An unknown IOS_IoctlAsync, or one that is not a prologue.
    riftwii::IpcSymbols bad;
    EXPECT_FALSE(riftwii::find_ipc_api({t.range()}, bad, api, error));
    bad.ioctl_async = async_at[6] + 4;
    EXPECT_FALSE(riftwii::find_ipc_api({t.range()}, bad, api, error));

    // Two asynchronous functions storing the same command: refused.
    FakeText u;
    const std::uint32_t ualloc = u.here();
    u.prologue();
    const std::uint32_t usubmit = u.here();
    u.prologue();
    const std::uint32_t first = u.api_function(6, true, ualloc, usubmit);
    u.api_function(6, true, ualloc, usubmit);
    riftwii::IpcSymbols us;
    us.ioctl_async = first;
    riftwii::IpcApi uapi;
    EXPECT_FALSE(riftwii::find_ipc_api({u.range()}, us, uapi, error));
    EXPECT_TRUE(error.find("two asynchronous") != std::string::npos);

    // The known IOS_IoctlvAsync must agree with the classification.
    riftwii::IpcSymbols disagree = s;
    disagree.ioctlv_async = sync_at[7];
    EXPECT_FALSE(riftwii::find_ipc_api({t.range()}, disagree, api, error));
}

static void TestSymbolSearch() {
    FakeText t;
    // Functions first: IOS_IoctlAsync at +0, IOS_IoctlvAsync at +0x10, a decoy at +0x20.
    const std::uint32_t ioctl_async = t.here();
    t.ipc_function(6);
    const std::uint32_t ioctlv_async = t.here();
    t.ipc_function(7);
    const std::uint32_t decoy = t.here();
    t.prologue();
    // Call sites: five DI commands to ioctl_async, one 0x71 decoy elsewhere.
    for (std::uint32_t cmd : {0x71u, 0x70u, 0x8Au, 0x8Du, 0xE3u}) {
        t.li(4, cmd);
        t.nop();
        t.li(5, 0x20);
        t.bl(ioctl_async);
    }
    t.li(4, 0x71);
    t.bl(decoy);
    // Open partition through ioctlv_async.
    t.li(4, 0x8B);
    t.li(5, 3);
    t.li(6, 2);
    t.nop();
    t.bl(ioctlv_async);
    // A 0x8B without vector counts must not count.
    t.li(4, 0x8B);
    t.bl(decoy);

    riftwii::IpcSymbols s;
    std::string error;
    EXPECT_TRUE(riftwii::find_ipc_symbols({t.range()}, s, error));
    EXPECT_EQ(s.ioctl_async, ioctl_async);
    EXPECT_EQ(s.ioctl_async_commands, 5u);
    EXPECT_EQ(s.ioctlv_async, ioctlv_async);

    // Too few agreeing commands.
    EXPECT_FALSE(riftwii::find_ipc_symbols({t.range()}, s, error, 6));

    // Without a 0x71 site nothing qualifies.
    FakeText u;
    const std::uint32_t f = u.here();
    u.prologue();
    for (std::uint32_t cmd : {0x70u, 0x8Au, 0x8Du}) {
        u.li(4, cmd);
        u.bl(f);
    }
    EXPECT_FALSE(riftwii::find_ipc_symbols({u.range()}, s, error));

    // A target outside the text (e.g. into data) is rejected.
    FakeText v;
    for (std::uint32_t cmd : {0x71u, 0x70u, 0x8Au}) {
        v.li(4, cmd);
        v.bl(0x80300000);
    }
    EXPECT_FALSE(riftwii::find_ipc_symbols({v.range()}, s, error));

    // A candidate that does not start with stwu is rejected.
    FakeText w;
    const std::uint32_t g = w.here();
    w.nop();
    w.nop();
    for (std::uint32_t cmd : {0x71u, 0x70u, 0x8Au}) {
        w.li(4, cmd);
        w.bl(g);
    }
    EXPECT_FALSE(riftwii::find_ipc_symbols({w.range()}, s, error));

    // A helper called right after the command load by more sites than
    // IOS_IoctlAsync itself (as a flush or lock helper would be) does not
    // win: it has no IPC command number in its body.
    FakeText x;
    const std::uint32_t helper = x.here();
    x.prologue();
    const std::uint32_t real = x.here();
    x.ipc_function(6);
    for (std::uint32_t cmd : {0x71u, 0x70u, 0x8Au, 0x8Du, 0xE3u, 0xE4u}) {
        x.li(4, cmd);
        x.bl(helper);
    }
    for (std::uint32_t cmd : {0x71u, 0x70u, 0x8Au}) {
        x.li(4, cmd);
        x.bl(real);
    }
    EXPECT_TRUE(riftwii::find_ipc_symbols({x.range()}, s, error));
    EXPECT_EQ(s.ioctl_async, real);
    EXPECT_EQ(s.ioctl_async_commands, 3u);

    // The ioctlv vector counts may be loaded before the command.
    FakeText y;
    const std::uint32_t ya = y.here();
    y.ipc_function(6);
    const std::uint32_t yv = y.here();
    y.ipc_function(7);
    for (std::uint32_t cmd : {0x71u, 0x70u, 0x8Au}) {
        y.li(4, cmd);
        y.bl(ya);
    }
    y.li(6, 2);
    y.li(5, 3);
    y.li(4, 0x8B);
    y.bl(yv);
    EXPECT_TRUE(riftwii::find_ipc_symbols({y.range()}, s, error));
    EXPECT_EQ(s.ioctlv_async, yv);

    // Two text ranges: sites in one, function in the other.
    FakeText fn;
    fn.base = 0x80100000;
    const std::uint32_t h = fn.here();
    fn.ipc_function(6);
    FakeText sites;
    for (std::uint32_t cmd : {0x71u, 0x70u, 0xE4u}) {
        sites.li(4, cmd);
        sites.bl(h);
    }
    EXPECT_TRUE(riftwii::find_ipc_symbols({sites.range(), fn.range()}, s, error));
    EXPECT_EQ(s.ioctl_async, h);
    EXPECT_EQ(s.ioctlv_async, 0u);

    // Misaligned range is refused.
    riftwii::CodeRange bad = t.range();
    bad.address = 0x80004002;
    EXPECT_FALSE(riftwii::find_ipc_symbols({bad}, s, error));
}

// ---- resident C code (compiled for the host) ------------------------------

static void TestFsIpcTranslation() {
    const std::uintptr_t args[8] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u,
                                    0x55555555u, 0x66666666u, 0x77777777u, 0x88888888u};
    for (std::uint32_t entry = 0; entry < RT_IPC_ENTRIES; ++entry) {
        const std::uint32_t command = entry % RT_IPC_COMMANDS + 1u;
        const bool async = entry < RT_IPC_COMMANDS;
        rtfs_ipc ipc{};
        EXPECT_TRUE(rt_build_fs_ipc(entry, args, &ipc));
        EXPECT_EQ(ipc.command, command);
        if (command == RTFS_CMD_OPEN) {
            EXPECT_EQ(ipc.args.open.path, 0x11111111u);
            EXPECT_EQ(ipc.args.open.mode, 0x22222222u);
            EXPECT_EQ(ipc.callback, async ? 0x33333333u : 0u);
            EXPECT_EQ(ipc.user_data, async ? 0x44444444u : 0u);
        } else {
            EXPECT_EQ(static_cast<std::uint32_t>(ipc.fd), 0x11111111u);
            if (command == RTFS_CMD_CLOSE) {
                EXPECT_EQ(ipc.callback, async ? 0x22222222u : 0u);
                EXPECT_EQ(ipc.user_data, async ? 0x33333333u : 0u);
            } else if (command == RTFS_CMD_READ || command == RTFS_CMD_WRITE) {
                EXPECT_EQ(ipc.args.readwrite.data, 0x22222222u);
                EXPECT_EQ(ipc.args.readwrite.length, 0x33333333u);
                EXPECT_EQ(ipc.callback, async ? 0x44444444u : 0u);
                EXPECT_EQ(ipc.user_data, async ? 0x55555555u : 0u);
            } else if (command == RTFS_CMD_SEEK) {
                EXPECT_EQ(static_cast<std::uint32_t>(ipc.args.seek.where), 0x22222222u);
                EXPECT_EQ(ipc.args.seek.whence, 0x33333333u);
                EXPECT_EQ(ipc.callback, async ? 0x44444444u : 0u);
                EXPECT_EQ(ipc.user_data, async ? 0x55555555u : 0u);
            } else if (command == RTFS_CMD_IOCTL) {
                EXPECT_EQ(ipc.args.ioctl.request, 0x22222222u);
                EXPECT_EQ(ipc.args.ioctl.in, 0x33333333u);
                EXPECT_EQ(ipc.args.ioctl.in_len, 0x44444444u);
                EXPECT_EQ(ipc.args.ioctl.out, 0x55555555u);
                EXPECT_EQ(ipc.args.ioctl.out_len, 0x66666666u);
                EXPECT_EQ(ipc.callback, async ? 0x77777777u : 0u);
                EXPECT_EQ(ipc.user_data, async ? 0x88888888u : 0u);
            } else {
                EXPECT_EQ(ipc.args.ioctlv.request, 0x22222222u);
                EXPECT_EQ(ipc.args.ioctlv.in_count, 0x33333333u);
                EXPECT_EQ(ipc.args.ioctlv.out_count, 0x44444444u);
                EXPECT_EQ(ipc.args.ioctlv.vectors, 0x55555555u);
                EXPECT_EQ(ipc.callback, async ? 0x66666666u : 0u);
                EXPECT_EQ(ipc.user_data, async ? 0x77777777u : 0u);
            }
        }
    }
    rtfs_ipc bad;
    std::memset(&bad, 0xA5, sizeof(bad));
    EXPECT_FALSE(rt_build_fs_ipc(14, args, &bad));
    for (const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(&bad); p != reinterpret_cast<const std::uint8_t*>(&bad) + sizeof(bad); ++p)
        EXPECT_EQ(*p, 0u);
    std::memset(&bad, 0xA5, sizeof(bad));
    EXPECT_FALSE(rt_build_fs_ipc(0xFFFFFFFFu, args, &bad));
    for (const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(&bad); p != reinterpret_cast<const std::uint8_t*>(&bad) + sizeof(bad); ++p)
        EXPECT_EQ(*p, 0u);
    std::memset(&bad, 0xA5, sizeof(bad));
    EXPECT_FALSE(rt_build_fs_ipc(0, nullptr, &bad));
    for (const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(&bad); p != reinterpret_cast<const std::uint8_t*>(&bad) + sizeof(bad); ++p)
        EXPECT_EQ(*p, 0u);
    EXPECT_FALSE(rt_build_fs_ipc(0, args, nullptr));
}

// ---- synchronous savegame interception (slice 4B2) -------------------------

namespace {

std::uint32_t FsAddr(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

struct FsCard {
    fatimg::Image image{512, 2, 0};
    std::uint32_t transfers = 0;
    int transfer(std::uint32_t lba, std::uint32_t count, std::uint32_t buffer, std::uint32_t is_write) {
        ++transfers;
        const std::uint64_t at = std::uint64_t(lba) * 512;
        const std::uint64_t bytes = std::uint64_t(count) * 512;
        if (count == 0 || at + bytes > image.bytes.size()) return -1;
        auto* p = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(buffer));
        if (is_write) std::memcpy(image.bytes.data() + at, p, static_cast<std::size_t>(bytes));
        else std::memcpy(p, image.bytes.data() + at, static_cast<std::size_t>(bytes));
        return 0;
    }
};
FsCard* g_fs_card = nullptr;
std::int32_t FsTransfer(std::uint32_t lba, std::uint32_t count, std::uint32_t buffer, std::uint32_t is_write) {
    if (!g_fs_card) return -1;
    return g_fs_card->transfer(lba, count, buffer, is_write);
}

void FsCopyPath(std::uint8_t* out, const std::string& path) {
    std::memset(out, 0, RTFS_PATH_BYTES);
    std::memcpy(out, path.c_str(), path.size());
}

}  // namespace

static void TestFsSyncIntercept() {
    std::uint8_t* low = LowBuffer(0x10000);
    if (!low) {
        std::cerr << "note: no 32-bit addressable buffer on this host, skipping the FS intercept drive" << std::endl;
        return;
    }
    // Card image: save directory at cluster 3 holding SAVE BIN (2 clusters
    // of known bytes), mirroring the rtfs test fixture's shape.
    FsCard card;
    const std::uint32_t kClusterBytes = card.image.cluster_bytes();
    fatimg::Bytes content(kClusterBytes + 37);
    for (std::size_t i = 0; i < content.size(); ++i) content[i] = static_cast<std::uint8_t>(i * 7 + 3);
    card.image.write_data({10, 11}, content);
    fatimg::Bytes dir;
    fatimg::Bytes save = fatimg::short_entry("SAVE    BIN", 0x20, 10, static_cast<std::uint32_t>(content.size()), 0x18);
    dir.insert(dir.end(), save.begin(), save.end());
    EXPECT_TRUE(card.image.write_dir({3}, dir));

    rtfat_volume volume{};
    volume.sectors_per_cluster = card.image.spc;
    volume.fat_lba = card.image.reserved;
    volume.fat_count = card.image.fats;
    volume.fat_sectors = card.image.fat_sectors;
    volume.data_lba = static_cast<std::uint32_t>(card.image.data_start_sector());
    volume.cluster_count = card.image.clusters;
    volume.dir_cluster = 3;
    volume.alloc_hint = 13;

    // Game memory below 4 GiB: interception state, path strings, file data.
    // Every pointer the engine touches must live down here: it addresses
    // memory through 32-bit fields, so a stack buffer would truncate.
    auto* st = reinterpret_cast<rt_fs_state*>(low);
    std::uint8_t* path = low + 0x3000;
    std::uint8_t* data = low + 0x4000;
    auto* out = reinterpret_cast<std::uint32_t*>(low + 0x5000);
    out[0] = 0; out[1] = 0;
    std::memset(st, 0, sizeof(*st));
    const std::string prefix = "/title/00010000/524d4345/data";
    EXPECT_EQ(rtfs_init(&st->fs, &volume, prefix.c_str(), -1), RTFAT_OK);

    rt_context ctx{};
    ctx.magic = RT_CONTEXT_MAGIC;
    ctx.flags = RT_FLAG_FS;
    ctx.fs_state = FsAddr(st);
    g_fs_card = &card;
    rt_host_fs_transfer = FsTransfer;

    std::uintptr_t args[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::uint32_t result = 0xDEADu;
    const std::uint32_t hijacked0 = ctx.fs_hijacked;

    // Open the save file: hijacked with a fake fd, and the lookup needed
    // card transfers (the NEEDS_IO drive ran, not just an immediate answer).
    FsCopyPath(path, prefix + "/save.bin");
    const std::uint32_t transfers0 = card.transfers;
    args[0] = FsAddr(path); args[1] = 3;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
    EXPECT_TRUE(static_cast<std::int32_t>(result) >= static_cast<std::int32_t>(RTFS_FD_BASE));
    EXPECT_TRUE(card.transfers > transfers0);
    EXPECT_EQ(ctx.fs_hijacked, hijacked0 + 1u);
    const std::uint32_t fd = result;

    // Read it back through the dispatcher: bytes match the card image.
    std::memset(data, 0xEE, 512);
    args[0] = fd; args[1] = FsAddr(data); args[2] = 64;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(3), args, &result), 1);
    EXPECT_EQ(result, 64u);
    EXPECT_TRUE(std::memcmp(data, content.data(), 64) == 0);

    // The read left the position at 64, so seek home, overwrite through the
    // dispatcher, seek home again, read back: the write stuck.
    args[0] = fd; args[1] = 0; args[2] = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(5), args, &result), 1);
    EXPECT_EQ(result, 0u);
    for (int i = 0; i < 16; ++i) data[i] = static_cast<std::uint8_t>(0xC0 + i);
    args[0] = fd; args[1] = FsAddr(data); args[2] = 16;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(4), args, &result), 1);
    EXPECT_EQ(result, 16u);
    args[0] = fd; args[1] = 0; args[2] = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(5), args, &result), 1);
    EXPECT_EQ(result, 0u);
    std::memset(data, 0, 16);
    args[0] = fd; args[1] = FsAddr(data); args[2] = 16;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(3), args, &result), 1);
    EXPECT_EQ(result, 16u);
    for (int i = 0; i < 16; ++i) EXPECT_EQ(data[i], static_cast<std::uint8_t>(0xC0 + i));

    // File stats through the fake-fd ioctl: size and position.
    args[0] = fd; args[1] = 0x0B; args[2] = 0; args[3] = 0; args[4] = FsAddr(out); args[5] = 8;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(out[0], static_cast<std::uint32_t>(content.size()));
    EXPECT_EQ(out[1], 16u);

    // Close, then read-after-close fails from the dispatcher, not the card.
    args[0] = fd;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(2), args, &result), 1);
    EXPECT_EQ(result, 0u);
    args[0] = fd; args[1] = FsAddr(data); args[2] = 16;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(3), args, &result), 1);
    EXPECT_TRUE(static_cast<std::int32_t>(result) < 0);

    // Missing file: hijacked NOT_FOUND, never replayed.
    FsCopyPath(path, prefix + "/gone.bin");
    args[0] = FsAddr(path); args[1] = 3;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), RTFAT_ENOENT);

    // Outside the prefix: replayed untouched.
    FsCopyPath(path, "/title/00010000/524d4541/data/save.bin");
    args[0] = FsAddr(path); args[1] = 3;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 0);
    FsCopyPath(path, "/dev/fs");
    args[0] = FsAddr(path); args[1] = 3;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 0);

    // An ISFS ioctl on a real fd replays in 4B2 (fd learning is 4B3's work).
    args[0] = 5; args[1] = 0x09; args[2] = FsAddr(path); args[3] = 64; args[4] = 0; args[5] = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 0);

    // Async forms still replay in 4B2, even for in-prefix paths.
    FsCopyPath(path, prefix + "/save.bin");
    args[0] = FsAddr(path); args[1] = 3; args[2] = 0x80001000; args[3] = 0x80002000;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 0);

    // Flag off: replay. No state block: replay.
    ctx.flags = 0;
    args[0] = FsAddr(path); args[1] = 3;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 0);
    ctx.flags = RT_FLAG_FS;
    ctx.fs_state = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 0);
    ctx.fs_state = FsAddr(st);

    // Busy engine (a re-entrant arrival): hijacked access error, then recovery.
    st->fs.busy = 1;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), RTFAT_EACCESS);
    st->fs.busy = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
    EXPECT_TRUE(static_cast<std::int32_t>(result) >= static_cast<std::int32_t>(RTFS_FD_BASE));
    args[0] = result;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(2), args, &result), 1);

    rt_host_fs_transfer = nullptr;
    g_fs_card = nullptr;
}

static void TestResidentHandler() {
    rt_context ctx{};
    ctx.magic = RT_CONTEXT_MAGIC;
    std::uint32_t di_cmd[8] = {0x71000000, 0x8000, 0x12345, 0, 0, 0, 0, 0};
    std::uintptr_t args[8] = {5, 0x71, reinterpret_cast<std::uintptr_t>(di_cmd), 0x20, 0x80400000, 0x8000, 0x80005000, 0};
    std::uint32_t result = 0;
    EXPECT_TRUE(rt_is_di_read(0x71, di_cmd, 0x20));
    EXPECT_FALSE(rt_is_di_read(0x70, di_cmd, 0x20));
    EXPECT_FALSE(rt_is_di_read(0x71, di_cmd, 0x1C));
    EXPECT_FALSE(rt_is_di_read(0x71, nullptr, 0x20));
    di_cmd[0] = 0x70000000;
    EXPECT_FALSE(rt_is_di_read(0x71, di_cmd, 0x20));
    di_cmd[0] = 0x71000000;

    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 0);
    EXPECT_EQ(ctx.ioctl_async_calls, 0u);
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC_IOCTL, args, &result), 0);
    EXPECT_EQ(ctx.ioctl_async_calls, 1u);
    EXPECT_EQ(ctx.di_reads, 1u);
    EXPECT_EQ(ctx.di_read_bytes_lo, 0x8000u);
    EXPECT_EQ(ctx.di_fd, 5u);
    EXPECT_EQ(ctx.last_di_word_offset, 0x12345u);
    EXPECT_EQ(ctx.last_di_length, 0x8000u);
    EXPECT_EQ(args[6], 0x80005000u);  // no table: untouched

    args[1] = 0x8A;  // a non-read ioctl counts as a call only
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    EXPECT_EQ(ctx.ioctl_async_calls, 2u);
    EXPECT_EQ(ctx.di_reads, 1u);

    // Byte counter carries into the high word.
    args[1] = 0x71;
    ctx.di_read_bytes_lo = 0xFFFFF000u;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    EXPECT_EQ(ctx.di_read_bytes_lo, 0x7000u);
    EXPECT_EQ(ctx.di_read_bytes_hi, 1u);

    // The Gecko path is a no-op on the host but must not fail.
    ctx.flags = RT_FLAG_GECKO;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    EXPECT_EQ(ctx.gecko_failures, 0u);
    EXPECT_EQ(sizeof(rt_context), riftwii::kResidentContextBytes);
    EXPECT_EQ(rt_checksum(reinterpret_cast<const std::uint8_t*>("ab"), 2), 97u * 31u + 98u);
}

// ---- SD-backed runs: the completion entry chains CMD18 requests ---------

namespace {

riftwii::PayloadPieces Pieces(std::vector<riftwii::MemReplacement> mem, std::vector<riftwii::SdReplacement> sd,
                              std::vector<riftwii::DiscReplacement> disc) {
    riftwii::PayloadPieces p;
    p.mem = std::move(mem);
    p.sd = std::move(sd);
    p.disc = std::move(disc);
    return p;
}

// The fake card: 128 sectors whose byte k of sector s is (s * 7 + k) & 0xFF.
std::uint8_t g_card[128 * 512];
std::vector<std::uint32_t> g_sd_sectors_requested;  // (sector, count) pairs

std::int32_t FakeIoctlvAsync(std::uint32_t fd, std::uint32_t ioctl, std::uint32_t in_count, std::uint32_t out_count,
                             rt_ioctlv* vec, std::uint32_t callback, rt_pending* record) {
    EXPECT_EQ(fd, 9u);
    EXPECT_EQ(ioctl, 7u);
    EXPECT_EQ(in_count, 2u);
    EXPECT_EQ(out_count, 1u);
    EXPECT_EQ(callback, 0x935D0100u);
    const auto* rq = reinterpret_cast<const rt_sdio_request*>(static_cast<std::uintptr_t>(vec[0].data));
    EXPECT_EQ(vec[0].len, 36u);
    EXPECT_EQ(rq->cmd, 0x12u);
    EXPECT_EQ(rq->cmd_type, 3u);
    EXPECT_EQ(rq->rsp_type, 1u);
    EXPECT_EQ(rq->blk_size, 512u);
    EXPECT_EQ(rq->isdma, 1u);
    EXPECT_EQ(rq->dma_addr, record->bounce);
    EXPECT_EQ(vec[1].data, record->bounce);
    EXPECT_EQ(vec[1].len, rq->blk_cnt * 512u);
    EXPECT_EQ(vec[2].len, 16u);
    const std::uint32_t sector = rq->arg;  // sdhc: sector number
    g_sd_sectors_requested.push_back(sector);
    g_sd_sectors_requested.push_back(rq->blk_cnt);
    if (sector + rq->blk_cnt > 128) return -4;  // past the card: refused
    std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(record->bounce)), g_card + sector * 512,
                rq->blk_cnt * 512);
    return 0;  // the reply is delivered by the test, like the IPC dispatcher would
}

// The fake drive: partition byte n reads as (n * 3) & 0xFF.
std::vector<std::uint32_t> g_disc_requests;  // (word offset, length) pairs

std::int32_t FakeIoctlAsync(std::uint32_t fd, std::uint32_t ioctl, std::uint32_t* in, std::uint32_t in_len,
                            std::uint32_t out, std::uint32_t out_len, std::uint32_t callback, rt_pending* record) {
    EXPECT_EQ(fd, 3u);  // the fd the game used
    EXPECT_EQ(ioctl, 0x71u);
    EXPECT_EQ(in_len, 0x20u);
    EXPECT_EQ(callback, 0x935D0100u);
    EXPECT_EQ(in, record->di_command);
    EXPECT_EQ(in[0], 0x71000000u);
    EXPECT_EQ(in[1], out_len);
    EXPECT_EQ(out, record->bounce);
    EXPECT_EQ(out_len % 32, 0u);
    g_disc_requests.push_back(in[2]);
    g_disc_requests.push_back(in[1]);
    std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(out));
    for (std::uint32_t k = 0; k < out_len; ++k) dst[k] = static_cast<std::uint8_t>(((in[2] << 2) + k) * 3);
    return 0;  // accepted; the reply is delivered by the test
}

}  // namespace

static void TestSdChain(std::uint8_t* low_table, std::uint8_t* low_out) {
    for (std::size_t s = 0; s < 128; ++s) {
        for (std::size_t k = 0; k < 512; ++k) g_card[s * 512 + k] = static_cast<std::uint8_t>(s * 7 + k);
    }
    // A 2000-byte file starting 100 bytes into sector 10, in two fragments:
    // sectors 10-11 (1024 - 100 = 924 usable bytes... the placer decides),
    // then sectors 40-42.
    std::vector<riftwii::Fragment> frags = {{10, 2}, {40, 3}};
    std::vector<riftwii::PlacedRun> runs;
    std::string error;
    EXPECT_TRUE(riftwii::place_on_fragments(frags, 100, 2000, runs, error));
    EXPECT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].source, 10ull);
    EXPECT_EQ(runs[0].skip, 100u);
    EXPECT_EQ(runs[0].length, 924ull);
    EXPECT_EQ(runs[1].source, 40ull);
    EXPECT_EQ(runs[1].length, 1076ull);
    // The bytes the game must see, from the fake card.
    std::vector<std::uint8_t> expected;
    expected.insert(expected.end(), g_card + 10 * 512 + 100, g_card + 12 * 512);
    expected.insert(expected.end(), g_card + 40 * 512, g_card + 40 * 512 + 1076);
    EXPECT_EQ(expected.size(), 2000u);

    riftwii::SdReplacement sdr;
    sdr.virtual_offset = 0x20000;
    sdr.runs = runs;
    riftwii::MemReplacement m;
    m.virtual_offset = 0x20000 + 2000;  // right after: 16 bytes from memory
    m.bytes.assign(16, 0x5A);
    std::vector<std::uint8_t> payload;
    const std::uint32_t table_address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(low_table));
    EXPECT_TRUE(riftwii::build_payload(Pieces({m}, {sdr}, {}), table_address, 0, 9, payload, error));
    const auto* header = reinterpret_cast<const rt_header*>(payload.data());
    EXPECT_EQ(header->entry_count, 3u);
    EXPECT_EQ(header->sdio_fd, 9u);
    EXPECT_EQ(rt_entries(header)[0].kind, static_cast<std::uint32_t>(RT_KIND_SD));
    EXPECT_EQ(rt_entries(header)[0].skip, 100ull);
    EXPECT_EQ(rt_entries(header)[1].vstart, 0x20000ull + 924);
    EXPECT_EQ(rt_entries(header)[2].kind, static_cast<std::uint32_t>(RT_KIND_MEM));
    std::memcpy(low_table, payload.data(), payload.size());

    // The records hand IOS 32-bit addresses of their own request, vector
    // and response blocks, so the context itself must sit below 4 GiB.
    rt_context& ctx = *reinterpret_cast<rt_context*>(low_table + 0x400);
    std::memset(&ctx, 0, sizeof(rt_context));
    ctx.magic = RT_CONTEXT_MAGIC;
    ctx.flags = RT_FLAG_GECKO;
    ctx.table = table_address;
    ctx.complete_entry = 0x935D0100;
    ctx.sdio_fd = 9;
    ctx.sdio_sdhc = 1;
    ctx.ioctlv_async = 0x8019445C;
    // The bounce buffer must be 32-bit addressable too: carve it from the low buffer.
    std::uint8_t* bounce = low_out + 0x1000;
    for (auto& p : ctx.pending) p.bounce = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bounce));
    rt_host_ioctlv_async = &FakeIoctlvAsync;

    // A 2048-byte read at 0x20000 - 16: 16 bytes of disc, the SD file, the 16 MEM bytes, 16 bytes of disc.
    std::uint8_t* out = low_out;
    std::memset(out, 0xEE, 0x800);
    std::uint32_t di_cmd[8] = {0x71000000, 0x800, (0x20000 - 16) >> 2, 0, 0, 0, 0, 0};
    std::uintptr_t args[8] = {3, 0x71, reinterpret_cast<std::uintptr_t>(di_cmd), 0x20,
                              reinterpret_cast<std::uintptr_t>(out), 0x800, 0x80005000, 0x80006000};
    std::uint32_t result = 0;
    g_sd_sectors_requested.clear();
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    auto* rec = reinterpret_cast<rt_pending*>(args[7]);
    EXPECT_EQ(rec->run_count, 5u);
    // Disc reply: MEM applied, first SD chunk issued, game not yet called.
    std::uintptr_t cb = 0xFFFF, ud = 0;
    std::int32_t di_result = 1;
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0u);
    EXPECT_EQ(rec->phase, RT_PHASE_SD);
    EXPECT_EQ(out[16 + 2000], 0x5A);
    EXPECT_EQ(g_sd_sectors_requested.size(), 2u);
    EXPECT_EQ(g_sd_sectors_requested[0], 10u);  // sector 10, 2 sectors (924 bytes from byte 100)
    EXPECT_EQ(g_sd_sectors_requested[1], 2u);
    EXPECT_EQ(rec->chunk_skip, 100u);
    EXPECT_EQ(rec->chunk_bytes, 924u);
    // SD reply 1: copied, second run issued.
    std::int32_t sd_result = 0;
    rt_on_di_complete(&ctx, &sd_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0u);
    EXPECT_EQ(g_sd_sectors_requested.size(), 4u);
    EXPECT_EQ(g_sd_sectors_requested[2], 40u);
    EXPECT_EQ(g_sd_sectors_requested[3], 3u);
    EXPECT_EQ(rec->chunk_skip, 0u);
    EXPECT_EQ(rec->chunk_bytes, 1076u);
    // SD reply 2: done, the game's callback with the disc result.
    sd_result = 0;
    rt_on_di_complete(&ctx, &sd_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0x80005000u);
    EXPECT_EQ(ud, 0x80006000u);
    EXPECT_EQ(sd_result, 1);
    EXPECT_EQ(rec->in_use, 0u);
    EXPECT_EQ(ctx.sd_requests, 2u);
    EXPECT_EQ(ctx.sd_failures, 0u);
    EXPECT_EQ(std::memcmp(out + 16, expected.data(), 2000), 0);
    EXPECT_EQ(out[15], 0xEE);
    EXPECT_EQ(out[16 + 2000 + 16], 0xEE);
    std::vector<std::uint8_t> ours(expected);
    ours.insert(ours.end(), m.bytes.begin(), m.bytes.end());
    EXPECT_EQ(ctx.last_checksum, rt_checksum(ours.data(), static_cast<std::uint32_t>(ours.size())));

    // A run longer than the bounce buffer is fetched in RT_BOUNCE_BYTES chunks.
    std::vector<riftwii::Fragment> big = {{0, 100}};  // 51200 bytes
    riftwii::SdReplacement sdb;
    sdb.virtual_offset = 0x40000;
    EXPECT_TRUE(riftwii::place_on_fragments(big, 0, 40000, sdb.runs, error));
    EXPECT_TRUE(riftwii::build_payload(Pieces({}, {sdb}, {}), table_address, 0, 9, payload, error));
    std::memcpy(low_table, payload.data(), payload.size());
    std::uint8_t* big_out = low_out + 0x1000 + RT_BOUNCE_BYTES + 0x100;  // 40000 bytes
    di_cmd[1] = 40000;
    di_cmd[2] = 0x40000 >> 2;
    args[4] = reinterpret_cast<std::uintptr_t>(big_out);
    args[5] = 40000;
    args[6] = 0x80005000;
    args[7] = 0x80006000;
    g_sd_sectors_requested.clear();
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    rec = reinterpret_cast<rt_pending*>(args[7]);
    di_result = 1;
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    int replies = 0;
    while (cb == 0 && replies < 10) {
        sd_result = 0;
        rt_on_di_complete(&ctx, &sd_result, rec, &cb, &ud);
        ++replies;
    }
    EXPECT_EQ(replies, 2);  // 32768 + 7232 bytes
    EXPECT_EQ(g_sd_sectors_requested.size(), 4u);
    EXPECT_EQ(g_sd_sectors_requested[1], 64u);
    EXPECT_EQ(g_sd_sectors_requested[2], 64u);
    EXPECT_EQ(g_sd_sectors_requested[3], 15u);  // 7232 bytes = 14.125 sectors
    EXPECT_EQ(std::memcmp(big_out, g_card, 40000), 0);
    EXPECT_EQ(cb, 0x80005000u);

    // A refused request ends the read with a DI error for the game.
    ctx.ioctlv_async = 0;  // "unknown": every request is refused
    std::memset(out, 0xEE, 0x800);
    di_cmd[1] = 0x800;
    di_cmd[2] = (0x20000 - 16) >> 2;
    args[4] = reinterpret_cast<std::uintptr_t>(out);
    args[5] = 0x800;
    args[6] = 0x80005000;
    args[7] = 0x80006000;
    EXPECT_TRUE(riftwii::build_payload(Pieces({m}, {sdr}, {}), table_address, 0, 9, payload, error));
    std::memcpy(low_table, payload.data(), payload.size());
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    rec = reinterpret_cast<rt_pending*>(args[7]);
    di_result = 1;
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0x80005000u);
    EXPECT_EQ(di_result, RT_DI_ERROR);
    EXPECT_EQ(ctx.sd_failures, 1u);
    EXPECT_EQ(rec->in_use, 0u);

    // A failed reply likewise.
    ctx.ioctlv_async = 0x8019445C;
    args[6] = 0x80005000;
    args[7] = 0x80006000;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    rec = reinterpret_cast<rt_pending*>(args[7]);
    di_result = 1;
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0u);
    sd_result = -5;
    rt_on_di_complete(&ctx, &sd_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0x80005000u);
    EXPECT_EQ(sd_result, RT_DI_ERROR);
    EXPECT_EQ(ctx.sd_failures, 2u);
    EXPECT_EQ(rec->in_use, 0u);

    // A failed disc read issues nothing.
    args[6] = 0x80005000;
    args[7] = 0x80006000;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    rec = reinterpret_cast<rt_pending*>(args[7]);
    di_result = 2;
    g_sd_sectors_requested.clear();
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0x80005000u);
    EXPECT_EQ(di_result, 2);
    EXPECT_EQ(g_sd_sectors_requested.size(), 0u);
    rt_host_ioctlv_async = nullptr;

    // DISC runs: a 100-byte range at partition byte 0x7005 relocated to
    // 0x60000, in a read of 0x80 bytes at 0x60000 (so 100 disc bytes then
    // a 28-byte gap). The runtime issues a DVDLowRead of the 32-byte
    // aligned span [0x7000, 0x7080) into the bounce buffer through the
    // unhooked entry, on the game's DI fd.
    riftwii::DiscReplacement dr;
    dr.virtual_offset = 0x60000;
    dr.disc_offset = 0x7005;
    dr.length = 100;
    EXPECT_TRUE(riftwii::build_payload(Pieces({}, {}, {dr}), table_address, 0, 9, payload, error));
    std::memcpy(low_table, payload.data(), payload.size());
    ctx.di_read_entry = 0x935D00AC;
    rt_host_ioctl_async = &FakeIoctlAsync;
    g_disc_requests.clear();
    std::memset(out, 0xEE, 0x800);
    di_cmd[1] = 0x80;
    di_cmd[2] = 0x60000 >> 2;
    args[4] = reinterpret_cast<std::uintptr_t>(out);
    args[5] = 0x80;
    args[6] = 0x80005000;
    args[7] = 0x80006000;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    EXPECT_EQ(ctx.di_fd, 3u);
    rec = reinterpret_cast<rt_pending*>(args[7]);
    di_result = 1;
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0u);
    EXPECT_EQ(rec->phase, RT_PHASE_DISC_RUN);
    EXPECT_EQ(g_disc_requests.size(), 2u);
    EXPECT_EQ(g_disc_requests[0], 0x7000u >> 2);  // word offset of the aligned start
    EXPECT_EQ(g_disc_requests[1], 0x80u);         // 5 + 100 = 105 bytes, rounded to 32
    EXPECT_EQ(rec->chunk_skip, 5u);
    EXPECT_EQ(rec->chunk_bytes, 100u);
    di_result = 1;  // the drive's reply
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0x80005000u);
    EXPECT_EQ(di_result, 1);
    for (std::uint32_t k = 0; k < 100; ++k) {
        if (out[k] != static_cast<std::uint8_t>((0x7005 + k) * 3)) {
            EXPECT_TRUE(false);
            break;
        }
    }
    EXPECT_EQ(out[100], 0xEE);  // beyond the DISC run: untouched (not a virtual read)
    EXPECT_EQ(ctx.disc_requests, 1u);
    EXPECT_EQ(ctx.disc_failures, 0u);

    // The drive's error is passed to the game as it is.
    args[6] = 0x80005000;
    args[7] = 0x80006000;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    rec = reinterpret_cast<rt_pending*>(args[7]);
    di_result = 1;
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0u);
    di_result = 4;  // e.g. a timeout
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0x80005000u);
    EXPECT_EQ(di_result, 4);
    EXPECT_EQ(ctx.disc_failures, 1u);
    EXPECT_EQ(rec->in_use, 0u);
    rt_host_ioctl_async = nullptr;
}

static void TestPayloadAndRedirect() {
    // Two replacements, given out of order; the table must come out sorted.
    riftwii::MemReplacement a, b;
    a.virtual_offset = 0x5F0FDBF0;  // like /hbm/home.csv
    a.bytes.assign(3610, 0x41);
    b.virtual_offset = 0x1000;
    b.bytes = {1, 2, 3, 4, 5};
    std::vector<std::uint8_t> payload;
    std::string error;
    EXPECT_TRUE(riftwii::build_mem_payload({a, b}, 0x935D2000, 7, payload, error));
    const auto* header = reinterpret_cast<const rt_header*>(payload.data());
    EXPECT_EQ(header->entry_count, 2u);
    EXPECT_EQ(header->tag, 7ull);
    EXPECT_EQ(rt_validate(header, payload.size()), RT_OK);
    const rt_entry* entries = rt_entries(header);
    EXPECT_EQ(entries[0].vstart, 0x1000ull);
    EXPECT_EQ(entries[0].length, 5ull);
    EXPECT_EQ(entries[0].kind, static_cast<std::uint32_t>(RT_KIND_MEM));
    EXPECT_EQ(entries[1].vstart, 0x5F0FDBF0ull);
    EXPECT_EQ(entries[1].length, 3610ull);
    // Sources point into the payload at 32-byte aligned data.
    const std::size_t table_bytes = (rt_table_bytes(2) + 31) & ~std::size_t(31);
    EXPECT_EQ(entries[0].source, 0x935D2000ull + table_bytes);
    EXPECT_EQ(entries[1].source, 0x935D2000ull + table_bytes + 32);
    EXPECT_EQ(payload[table_bytes], 1);
    EXPECT_EQ(payload[table_bytes + 32], 0x41);
    EXPECT_EQ(payload.size(), table_bytes + 32 + 3616);

    // Ready-made entries (the XML compiler's output) merge with the rest;
    // a MEM entry there has no bytes and is refused.
    riftwii::PayloadPieces mixed;
    mixed.mem = {b};
    rt_entry ready{};
    ready.vstart = 0x3000;
    ready.length = 0x100;
    ready.source = 0x2000;
    ready.kind = RT_KIND_DISC;
    mixed.entries.push_back(ready);
    ready.vstart = 0x2000;
    ready.length = 0x40;
    ready.source = 77;
    ready.skip = 5;
    ready.kind = RT_KIND_SD;
    mixed.entries.push_back(ready);
    EXPECT_TRUE(mixed.needs_sd());
    EXPECT_TRUE(riftwii::build_payload(mixed, 0x935D2000, 1, 4, payload, error));
    header = reinterpret_cast<const rt_header*>(payload.data());
    EXPECT_EQ(header->entry_count, 3u);
    EXPECT_EQ(header->sdio_fd, 4u);
    EXPECT_EQ(rt_entries(header)[0].kind, static_cast<std::uint32_t>(RT_KIND_MEM));
    EXPECT_EQ(rt_entries(header)[1].kind, static_cast<std::uint32_t>(RT_KIND_SD));
    EXPECT_EQ(rt_entries(header)[1].skip, 5ull);
    EXPECT_EQ(rt_entries(header)[2].kind, static_cast<std::uint32_t>(RT_KIND_DISC));
    ready.kind = RT_KIND_MEM;
    mixed.entries.push_back(ready);
    EXPECT_FALSE(riftwii::build_payload(mixed, 0x935D2000, 1, 4, payload, error));

    // Rejections.
    riftwii::MemReplacement empty;
    empty.virtual_offset = 0x9000;
    EXPECT_FALSE(riftwii::build_mem_payload({empty}, 0x935D2000, 0, payload, error));
    EXPECT_FALSE(riftwii::build_mem_payload({}, 0x935D2000, 0, payload, error));
    riftwii::MemReplacement c = b;
    c.virtual_offset = 0x1002;  // overlaps b
    EXPECT_FALSE(riftwii::build_mem_payload({b, c}, 0x935D2000, 0, payload, error));

    // Now drive the runtime against a copy of the payload placed below
    // 4 GiB (the runtime dereferences 32-bit MEM sources and table pointer).
    std::uint8_t* low = LowBuffer(0x30000);
    if (!low) {
        std::cerr << "note: no 32-bit addressable buffer on this host, skipping the redirect drive" << std::endl;
        return;
    }
    std::uint8_t* out = low;                 // the game's 0x20-byte destination
    std::uint8_t* table_copy = low + 0x1000;  // payload with real MEM sources
    const std::uint32_t table_address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(table_copy));
    EXPECT_TRUE(riftwii::build_mem_payload({b}, table_address, 0, payload, error));
    std::memcpy(table_copy, payload.data(), payload.size());
    EXPECT_EQ(rt_validate(reinterpret_cast<const rt_header*>(table_copy), payload.size()), RT_OK);

    rt_context ctx{};
    ctx.magic = RT_CONTEXT_MAGIC;
    ctx.table = table_address;
    ctx.complete_entry = 0x935D0100;

    // A read of 0x20 bytes at 0xFF0 (word 0x3FC) covers [0xFF0, 0x1010): a
    // passthrough gap, the 5 replaced bytes, then a gap.
    std::memset(out, 0xEE, 0x20);
    std::uint32_t di_cmd[8] = {0x71000000, 0x20, 0x3FC, 0, 0, 0, 0, 0};
    std::uintptr_t args[8] = {3, 0x71, reinterpret_cast<std::uintptr_t>(di_cmd), 0x20,
                              reinterpret_cast<std::uintptr_t>(out), 0x20, 0x80005000, 0x80006000};
    std::uint32_t result = 0;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    EXPECT_EQ(ctx.redirected_reads, 1u);
    EXPECT_EQ(args[6], 0x935D0100u);  // our completion entry replaced the callback
    auto* rec = reinterpret_cast<rt_pending*>(args[7]);
    EXPECT_TRUE(rec == &ctx.pending[0]);
    EXPECT_EQ(rec->in_use, 1u);
    EXPECT_EQ(rec->callback, 0x80005000u);
    EXPECT_EQ(rec->user_data, 0x80006000u);
    EXPECT_EQ(rec->length, 0x20u);
    EXPECT_EQ(rec->word_offset, 0x3FCu);

    // The "disc" read completed: the runtime rewrites the replaced run only.
    std::uintptr_t cb = 0, ud = 0;
    std::int32_t di_result = 1;
    ctx.flags = RT_FLAG_GECKO;  // the checksum diagnostics run only when reporting is on
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(di_result, 1);
    EXPECT_EQ(cb, 0x80005000u);
    EXPECT_EQ(ud, 0x80006000u);
    EXPECT_EQ(rec->in_use, 0u);
    EXPECT_EQ(out[0x0F], 0xEE);
    EXPECT_EQ(out[0x10], 1);
    EXPECT_EQ(out[0x14], 5);
    EXPECT_EQ(out[0x15], 0xEE);
    EXPECT_EQ(ctx.last_checksum, rt_checksum(b.bytes.data(), 5));
    EXPECT_EQ(ctx.completions, 1u);

    // A failed disc read is passed on untouched.
    std::memset(out, 0xEE, 0x20);
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    rec = reinterpret_cast<rt_pending*>(args[7]);
    di_result = -4;
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(di_result, -4);
    EXPECT_EQ(out[0x10], 0xEE);
    EXPECT_EQ(rec->in_use, 0u);

    // A read that misses the table is not redirected.
    di_cmd[2] = 0x2000 >> 2;
    args[6] = 0x80005000;
    args[7] = 0x80006000;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    EXPECT_EQ(args[6], 0x80005000u);
    EXPECT_EQ(ctx.redirected_reads, 2u);

    // With every record busy the read passes through and is counted.
    for (auto& p : ctx.pending) p.in_use = 1;
    di_cmd[2] = 0x3FC;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    EXPECT_EQ(args[6], 0x80005000u);
    EXPECT_EQ(ctx.pending_overflow, 1u);
    for (auto& p : ctx.pending) p.in_use = 0;

    // Virtual window: a read at word 0x80000000 of 0x40 bytes over a table
    // that covers only the first 5 bytes there. The command block's offset
    // is rewritten to 0 before the disc sees it; on completion the MEM run
    // is copied and the rest of the buffer zeroed rather than left as the
    // disc's bytes.
    riftwii::MemReplacement v;
    v.virtual_offset = riftwii::kVirtualWindowStart;
    v.bytes = {9, 8, 7, 6, 5};
    std::uint8_t* vtable = low + 0x4000;
    const std::uint32_t vtable_address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(vtable));
    EXPECT_TRUE(riftwii::build_mem_payload({v}, vtable_address, 0, payload, error));
    std::memcpy(vtable, payload.data(), payload.size());
    ctx.table = vtable_address;
    ctx.virtual_start_words = 0x80000000u;
    std::uint8_t* vout = low + 0x100;
    std::memset(vout, 0xEE, 0x40);
    std::uint32_t vcmd[8] = {0x71000000, 0x40, 0x80000000u, 0, 0, 0, 0, 0};
    std::uintptr_t vargs[8] = {3, 0x71, reinterpret_cast<std::uintptr_t>(vcmd), 0x20,
                               reinterpret_cast<std::uintptr_t>(vout), 0x40, 0x80005000, 0x80006000};
    EXPECT_EQ(rt_on_ioctl_async(&ctx, vargs, &result), 0);
    EXPECT_EQ(vcmd[2], 0u);  // the drive is asked for the partition start
    EXPECT_EQ(ctx.virtual_reads, 1u);
    rec = reinterpret_cast<rt_pending*>(vargs[7]);
    EXPECT_EQ(rec->is_virtual, 1u);
    EXPECT_EQ(rec->word_offset, 0x80000000u);  // the record keeps the virtual offset for the lookup
    di_result = 1;
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(vout[0], 9);
    EXPECT_EQ(vout[4], 5);
    EXPECT_EQ(vout[5], 0);     // virtual gap: zero, not the disc's 0xEE
    EXPECT_EQ(vout[0x3F], 0);
    EXPECT_EQ(cb, 0x80005000u);

    // Below the window a gap still means "the disc's bytes".
    std::memset(vout, 0xEE, 0x40);
    vcmd[2] = 0x3FC;
    vargs[6] = 0x80005000;
    vargs[7] = 0x80006000;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, vargs, &result), 0);
    EXPECT_EQ(vcmd[2], 0x3FCu);
    EXPECT_EQ(vargs[6], 0x80005000u);  // nothing of the table there: passed through

    TestSdChain(low + 0x8000, low + 0x9000);
}

static void TestVirtualWindow() {
    // A tiny Wii FST: root, dir "hbm", files "home.csv" (3610 bytes at 0x1000)
    // and "config.txt" (35 bytes at 0x2000).
    Bytes image;
    auto put_entry = [&](std::uint8_t flags, std::uint32_t name_off, std::uint32_t a, std::uint32_t b) {
        const std::size_t at = image.size();
        image.resize(at + 12);
        image[at] = flags;
        image[at + 1] = static_cast<std::uint8_t>(name_off >> 16);
        image[at + 2] = static_cast<std::uint8_t>(name_off >> 8);
        image[at + 3] = static_cast<std::uint8_t>(name_off);
        Put32(image, at + 4, a);
        Put32(image, at + 8, b);
    };
    put_entry(1, 0, 0, 4);         // root: 4 entries
    put_entry(1, 0, 0, 4);         // hbm/: parent 0, next 4
    put_entry(0, 4, 0x1000 >> 2, 3610);   // home.csv
    put_entry(0, 13, 0x2000 >> 2, 35);    // config.txt
    const char names[] = "hbm\0home.csv\0config.txt\0";
    image.insert(image.end(), names, names + sizeof(names) - 1);

    riftwii::Fst fst;
    std::string error;
    EXPECT_TRUE(riftwii::Fst::parse(image.data(), image.size(), true, fst, error));

    riftwii::VirtualFile grown;
    grown.disc_path = "/hbm/home.csv";
    grown.bytes.assign(5000, 0x42);
    riftwii::VirtualFile tiny;
    tiny.disc_path = "/hbm/config.txt";
    tiny.bytes = {1, 2, 3};
    riftwii::VirtualFile on_card;  // config.txt again would collide; use home.csv's neighbour
    std::vector<riftwii::MemReplacement> reps;
    std::vector<riftwii::SdReplacement> sd_reps;
    std::vector<riftwii::DiscReplacement> disc_reps;
    std::uint64_t end = riftwii::kVirtualWindowStart;
    EXPECT_TRUE(riftwii::plan_virtual_window(fst, {grown, tiny}, reps, sd_reps, disc_reps, end, error));
    EXPECT_EQ(reps.size(), 2u);
    EXPECT_EQ(sd_reps.size(), 0u);
    EXPECT_EQ(reps[0].virtual_offset, riftwii::kVirtualWindowStart);
    EXPECT_EQ(reps[0].bytes.size(), 5024u);  // padded to 32
    EXPECT_EQ(reps[0].bytes[4999], 0x42);
    EXPECT_EQ(reps[0].bytes[5000], 0);
    EXPECT_EQ(reps[1].virtual_offset, riftwii::kVirtualWindowStart + 5024);
    EXPECT_EQ(reps[1].bytes.size(), 32u);
    EXPECT_EQ(end, riftwii::kVirtualWindowStart + 5056);
    const std::uint32_t h = fst.find("/hbm/home.csv");
    EXPECT_EQ(fst.entries()[h].offset, riftwii::kVirtualWindowStart);
    EXPECT_EQ(fst.entries()[h].size, 5000u);
    const std::uint32_t c = fst.find("/hbm/config.txt");
    EXPECT_EQ(fst.entries()[c].size, 3u);

    // The rewritten table serialises with the >> 2 encoding of an 8 GiB offset.
    Bytes out;
    EXPECT_TRUE(fst.serialize(out, error));
    EXPECT_EQ(out.size(), image.size());
    EXPECT_EQ(out[2 * 12 + 4], 0x80);  // word 0x80000000
    EXPECT_EQ(out[2 * 12 + 5], 0x00);

    // Unknown path / directory are refused; the table is left as it was.
    riftwii::VirtualFile bad;
    bad.disc_path = "/nope";
    bad.bytes = {1};
    EXPECT_FALSE(riftwii::plan_virtual_window(fst, {bad}, reps, sd_reps, disc_reps, end, error));
    bad.disc_path = "/hbm";
    EXPECT_FALSE(riftwii::plan_virtual_window(fst, {bad}, reps, sd_reps, disc_reps, end, error));
    bad.disc_path = "/hbm/config.txt";
    bad.bytes.clear();
    EXPECT_FALSE(riftwii::plan_virtual_window(fst, {bad}, reps, sd_reps, disc_reps, end, error));  // no content

    // An SD-backed file takes a slot the same way; its size is the runs' total.
    riftwii::VirtualFile card;
    card.disc_path = "/hbm/config.txt";
    std::vector<riftwii::Fragment> frags = {{100, 3}};
    EXPECT_TRUE(riftwii::place_on_fragments(frags, 0, 1300, card.sd_runs, error));
    EXPECT_EQ(card.size(), 1300ull);
    riftwii::Fst fst2;
    EXPECT_TRUE(riftwii::Fst::parse(image.data(), image.size(), true, fst2, error));
    reps.clear();
    end = riftwii::kVirtualWindowStart;
    EXPECT_TRUE(riftwii::plan_virtual_window(fst2, {grown, card}, reps, sd_reps, disc_reps, end, error));
    EXPECT_EQ(reps.size(), 1u);
    EXPECT_EQ(sd_reps.size(), 1u);
    EXPECT_EQ(sd_reps[0].virtual_offset, riftwii::kVirtualWindowStart + 5024);
    EXPECT_EQ(sd_reps[0].runs[0].source, 100ull);
    EXPECT_EQ(fst2.entries()[c].size, 1300u);
    EXPECT_EQ(fst2.entries()[c].offset, riftwii::kVirtualWindowStart + 5024);
    EXPECT_EQ(end, riftwii::kVirtualWindowStart + 5024 + 1312);


    // The payload builder accepts window offsets and the walker resolves them.
    std::vector<std::uint8_t> payload;
    EXPECT_TRUE(riftwii::build_payload(Pieces(reps, sd_reps, disc_reps), 0x935C0000, 0, 4, payload, error));
    const auto* header = reinterpret_cast<const rt_header*>(payload.data());
    EXPECT_EQ(rt_validate(header, payload.size()), RT_OK);
    rt_run runs[4];
    std::uint32_t n = 0;
    EXPECT_EQ(rt_lookup(header, riftwii::kVirtualWindowStart + 5000, 64, runs, 4, &n), RT_OK);
    EXPECT_EQ(n, 2u);  // 24 bytes of padding from the MEM entry, then 40 of the SD file
    EXPECT_EQ(runs[0].kind, static_cast<std::uint32_t>(RT_KIND_MEM));
    EXPECT_EQ(runs[0].length, 24ull);
    EXPECT_EQ(runs[1].kind, static_cast<std::uint32_t>(RT_KIND_SD));
    EXPECT_EQ(runs[1].length, 40ull);
    EXPECT_EQ(runs[1].source, 100ull);
    // Past the SD file's end the window is a gap the runtime zero-fills.
    EXPECT_EQ(rt_lookup(header, riftwii::kVirtualWindowStart + 5024 + 1280, 64, runs, 4, &n), RT_OK);
    EXPECT_EQ(n, 2u);
    EXPECT_EQ(runs[0].length, 20ull);
    EXPECT_EQ(runs[1].kind, static_cast<std::uint32_t>(RT_KIND_PASSTHROUGH));
    EXPECT_EQ(runs[1].length, 44ull);

    // A file relocated as it is becomes a DISC replacement of its old bytes.
    riftwii::VirtualFile kept;
    kept.disc_path = "/hbm/home.csv";
    kept.original = true;
    riftwii::Fst fst3;
    EXPECT_TRUE(riftwii::Fst::parse(image.data(), image.size(), true, fst3, error));
    reps.clear();
    sd_reps.clear();
    end = 0;  // a cursor outside the window is refused
    EXPECT_FALSE(riftwii::plan_virtual_window(fst3, {kept}, reps, sd_reps, disc_reps, end, error));
    end = riftwii::kVirtualWindowStart + 0x100;  // and slots continue from where a caller left off
    EXPECT_TRUE(riftwii::plan_virtual_window(fst3, {kept}, reps, sd_reps, disc_reps, end, error));
    EXPECT_EQ(disc_reps.size(), 1u);
    EXPECT_EQ(disc_reps[0].virtual_offset, riftwii::kVirtualWindowStart + 0x100);
    EXPECT_EQ(disc_reps[0].disc_offset, 0x1000ull);
    EXPECT_EQ(disc_reps[0].length, 3610ull);
    EXPECT_EQ(fst3.entries()[h].offset, riftwii::kVirtualWindowStart + 0x100);
    EXPECT_EQ(end, riftwii::kVirtualWindowStart + 0x100 + 3616);
    EXPECT_EQ(fst3.entries()[h].size, 3610u);
    disc_reps.clear();
}

int main() {
    TestBlob();
    TestJumpAndDisplace();
    TestPlacement();
    TestSymbolSearch();
    TestIpcApiSearch();
    TestFsIpcTranslation();
    TestFsSyncIntercept();
    TestResidentHandler();
    TestPayloadAndRedirect();
    TestVirtualWindow();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "hook tests passed" << std::endl;
    return 0;
}
