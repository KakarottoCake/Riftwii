// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/fst.hpp"
#include "riftwii/hook.hpp"
#include "riftwii/symsearch.hpp"
#include "rt_hook.h"
#include "rtable.h"
#include "fat32_image.hpp"
#include "riftwii/fat32.hpp"

#include <algorithm>
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
    for (std::uintptr_t hint = 0x10000000; hint < 0x70000000; hint += 0x01000000) {
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
    Put32(b, 16 + RT_IPC_ENTRIES * 12 + 4, 0x604);  // savegame completion entry
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
    EXPECT_EQ(rb.complete_fs_offset, 0x604u);
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
    Put32(b, 16 + RT_IPC_ENTRIES * 12 + 4, 0xFFE);  // savegame completion entry past the end
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

    // The one-word hook: MKWii's IOS_Open (RMCE01) to a runtime at the top
    // of MEM1, forward and backward, and the edges of b's reach.
    std::uint32_t b = 0;
    EXPECT_TRUE(riftwii::encode_branch(0x80193740, 0x817BC640, b));
    EXPECT_EQ(b, 0x49628F00u);
    EXPECT_TRUE(riftwii::encode_branch(0x817BC640, 0x80193740, b));
    EXPECT_EQ(b, 0x4A9D7100u);
    EXPECT_TRUE(riftwii::encode_branch(0x80003100, 0x817FFFFC, b));  // all of MEM1 is in reach
    EXPECT_TRUE(riftwii::encode_branch(0x80000000, 0x81FFFFFC, b));
    EXPECT_EQ(b, 0x49FFFFFCu);
    EXPECT_FALSE(riftwii::encode_branch(0x80000000, 0x82000000, b));
    EXPECT_TRUE(riftwii::encode_branch(0x82000000, 0x80000000, b));
    EXPECT_EQ(b, 0x4A000000u);
    EXPECT_FALSE(riftwii::encode_branch(0x82000004, 0x80000000, b));
    EXPECT_FALSE(riftwii::encode_branch(0x80193742, 0x817BC640, b));
    EXPECT_EQ(riftwii::kHookStubBytes, 4u);  // a caller entering at +4 skips the hook

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
    EXPECT_TRUE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x90000800, 0x935E0000, 7840, 0, p, error));
    EXPECT_EQ(p.code_base, 0x817E7FC0u);
    EXPECT_EQ(p.code_bytes, 7840u);
    EXPECT_EQ(p.new_arena1_hi, 0x817E7FC0u);
    EXPECT_EQ(p.data_base, 0u);
    EXPECT_EQ(p.data_bytes, 0u);
    EXPECT_EQ(p.new_arena2_lo, 0x90000800u);
    // With data: the bottom of the MEM2 arena up to a 32-byte line, staged
    // at the top; the arena end is never moved.
    EXPECT_TRUE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x90000800, 0x935E0000, 7840, 1, p, error));
    EXPECT_EQ(p.data_base, 0x90000800u);
    EXPECT_EQ(p.data_bytes, 0x20u);
    EXPECT_EQ(p.new_arena2_lo, 0x90000820u);
    EXPECT_EQ(p.stage_base, 0x935DFFE0u);
    EXPECT_TRUE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x90000800, 0x935E0000, 7840, 0xF801, p, error));
    EXPECT_EQ(p.new_arena2_lo, 0x90010020u);
    EXPECT_EQ(p.data_bytes, 0xF820u);
    EXPECT_TRUE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x90000800, 0x933E0000, 7840, 0x20000, p, error));  // IOS58-style end
    EXPECT_EQ(p.new_arena2_lo, 0x90020800u);
    EXPECT_EQ(p.stage_base, 0x933C0000u);
    EXPECT_TRUE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x90000800, 0x935DFFE0, 7840, 32, p, error));  // unaligned end: stage rounds down
    EXPECT_EQ(p.stage_base, 0x935DFFC0u);
    EXPECT_FALSE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x90000810, 0x935E0000, 7840, 32, p, error));  // unaligned start
    EXPECT_FALSE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x80000800, 0x935E0000, 7840, 32, p, error));  // MEM1 as MEM2 start
    // The code must clear the floor (loader, apploader image).
    EXPECT_TRUE(riftwii::plan_resident_placement(0x81241EA0, 0x81240000, 0x90000800, 0x935E0000, 7840, 0, p, error));
    EXPECT_EQ(p.code_base, 0x81240000u);
    EXPECT_FALSE(riftwii::plan_resident_placement(0x81241E80, 0x81240000, 0x90000800, 0x935E0000, 7840, 0, p, error));
    EXPECT_FALSE(riftwii::plan_resident_placement(0x817E9E70, 0x81240000, 0x90000800, 0x935E0000, 7840, 0, p, error));  // unaligned top
    EXPECT_FALSE(riftwii::plan_resident_placement(0x00000000, 0x81240000, 0x90000800, 0x935E0000, 7840, 0, p, error));  // no arena top
    EXPECT_FALSE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x90000800, 0x935E0000, 7841, 0, p, error));  // odd blob
    EXPECT_FALSE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x90000800, 0x80000000, 7840, 32, p, error));  // MEM1 as MEM2 end
    EXPECT_FALSE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x90000800, 0x90800000, 7840, 32, p, error));  // at the floor
    EXPECT_FALSE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x90000800, 0x935E0000, 7840, 0x03000000, p, error));  // too big
    EXPECT_FALSE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0x90000800, 0x935E0000, 7840, 0x02000000, p, error));  // data and stage overlap
    EXPECT_TRUE(riftwii::plan_resident_placement(0x817E9E60, 0x81240000, 0, 0x80000000, 7840, 0, p, error));  // no data: MEM2 not looked at
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
    fatimg::Image image;
    explicit FsCard(std::uint32_t spc = 2, std::uint32_t clusters = 256) : image(512, spc, 0, clusters) {}
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
// Something to do once, in the middle of a synchronous card transfer:
// what an IPC callback of the game does while a thread-side request
// holds the engine.
void (*g_on_transfer)() = nullptr;
std::int32_t FsTransfer(std::uint32_t lba, std::uint32_t count, std::uint32_t buffer, std::uint32_t is_write) {
    if (!g_fs_card) return -1;
    if (g_on_transfer) {
        void (*once)() = g_on_transfer;
        g_on_transfer = nullptr;
        once();
    }
    return g_fs_card->transfer(lba, count, buffer, is_write);
}

void FsCopyPath(std::uint8_t* out, const std::string& path) {
    std::memset(out, 0, RTFS_PATH_BYTES);
    std::memcpy(out, path.c_str(), path.size());
}

// A completed import must leave no stage behind: the host reader sees
// hidden entries too, so any live ".rwstage.tmp" (or its RWSTAG alias)
// fails this. Guards the ghost entry once seen on a Dolphin card image,
// where a leftover stage shared its committed file's chain.
void ExpectNoStage(FsCard& card) {
    riftwii::Fat32Volume v;
    std::string err;
    EXPECT_TRUE(riftwii::Fat32Volume::mount(card.image.reader(), v, err));
    std::vector<riftwii::Fat32Entry> es;
    EXPECT_TRUE(v.list("/save", es, err));
    for (const auto& e : es) {
        EXPECT_TRUE(e.name != ".rwstage.tmp");
        EXPECT_TRUE(e.short_name.find("RWSTAG") == std::string::npos);
    }
}

struct CbLog {
    std::uint32_t cb = 0;
    std::int32_t result = 0;
    std::uint32_t ud = 0;
    std::uint32_t calls = 0;
};
CbLog* g_cb_log = nullptr;
void GameCb(std::uint32_t cb, std::int32_t result, std::uint32_t ud) {
    if (!g_cb_log) return;
    g_cb_log->cb = cb;
    g_cb_log->result = result;
    g_cb_log->ud = ud;
    g_cb_log->calls++;
}

// The console's IOS as the savegame path sees it: every request the
// runtime issues (a card transfer, a null round trip) is queued and
// completed later, oldest first, through the FS completion entry, whose
// tail call of the game's callback is recorded as a delivery.
// The fake NAND (below) answers the job's asynchronous requests when the
// fake IOS completes them.
std::int32_t NandOpenSync(const char* path, std::uint32_t mode);
std::int32_t NandCloseSync(std::int32_t fd);
std::int32_t NandReadSync(std::int32_t fd, std::uint32_t buffer, std::uint32_t length);
std::int32_t NandIoctlSync(std::int32_t fd, std::uint32_t request, std::uint32_t in, std::uint32_t in_len, std::uint32_t out,
                           std::uint32_t out_len);
struct FakeIos {
    // 1 transfer, 2 defer, 3 NAND open, 4 NAND close, 5 NAND read, 6 NAND ioctl
    struct Request {
        int kind = 0; void* tag = nullptr; std::uint32_t lba = 0, count = 0, buffer = 0, write = 0;
        std::string path; std::int32_t fd = -1; std::uint32_t a = 0, b = 0, c = 0, d = 0;
    };
    struct Delivery { std::uint32_t cb, ud; std::int32_t result; };
    FsCard* card = nullptr;
    rt_context* ctx = nullptr;
    std::vector<Request> queue;
    std::vector<Delivery> delivered;
    std::uint32_t refuse_issues = 0;   // refuse the next N transfer issues
    std::uint32_t refuse_defers = 0;   // refuse the next N null round trips
    std::uint32_t fail_transfers = 0;  // complete the next N transfers with an IPC error
    std::uint32_t nand_requests = 0;   // NAND requests completed
    bool complete_one() {
        if (queue.empty()) return false;
        const Request r = queue.front();
        queue.erase(queue.begin());
        std::int32_t ios = 0;
        if (r.kind == 1) {
            if (fail_transfers != 0) { --fail_transfers; ios = -4; }
            else ios = card->transfer(r.lba, r.count, r.buffer, r.write);
        } else if (r.kind == 3) { ios = NandOpenSync(r.path.c_str(), r.a); ++nand_requests; }
        else if (r.kind == 4) { ios = NandCloseSync(r.fd); ++nand_requests; }
        else if (r.kind == 5) { ios = NandReadSync(r.fd, r.buffer, r.a); ++nand_requests; }
        else if (r.kind == 6) { ios = NandIoctlSync(r.fd, r.a, r.buffer, r.b, r.c, r.d); ++nand_requests; }
        std::uintptr_t cb = 0xDEADu, ud = 0xDEADu;
        rt_on_fs_complete(ctx, &ios, r.tag, &cb, &ud);
        if (cb != 0) delivered.push_back({static_cast<std::uint32_t>(cb), static_cast<std::uint32_t>(ud), ios});
        return true;
    }
    void drain() { while (complete_one()) {} }
};
FakeIos* g_ios = nullptr;
std::int32_t IosIssue(std::uint32_t lba, std::uint32_t count, std::uint32_t buffer, std::uint32_t write, void* tag) {
    if (!g_ios) return -1;
    if (g_ios->refuse_issues != 0) { --g_ios->refuse_issues; return -1; }
    FakeIos::Request r; r.kind = 1; r.tag = tag; r.lba = lba; r.count = count; r.buffer = buffer; r.write = write;
    g_ios->queue.push_back(r);
    return 0;
}
std::int32_t IosDefer(void* tag) {
    if (!g_ios) return -1;
    if (g_ios->refuse_defers != 0) { --g_ios->refuse_defers; return -1; }
    FakeIos::Request r; r.kind = 2; r.tag = tag;
    g_ios->queue.push_back(r);
    return 0;
}
std::int32_t IosOpenAsync(const char* path, std::uint32_t mode, std::uint32_t, void* tag) {
    if (!g_ios) return -1;
    FakeIos::Request r; r.kind = 3; r.tag = tag; r.path = path; r.a = mode;
    g_ios->queue.push_back(r);
    return 0;
}
std::int32_t IosCloseAsync(std::int32_t fd, std::uint32_t, void* tag) {
    if (!g_ios) return -1;
    FakeIos::Request r; r.kind = 4; r.tag = tag; r.fd = fd;
    g_ios->queue.push_back(r);
    return 0;
}
std::int32_t IosReadAsync(std::int32_t fd, std::uint32_t buffer, std::uint32_t length, std::uint32_t, void* tag) {
    if (!g_ios) return -1;
    FakeIos::Request r; r.kind = 5; r.tag = tag; r.buffer = buffer; r.fd = fd; r.a = length;
    g_ios->queue.push_back(r);
    return 0;
}
std::int32_t IosIoctlAsync(std::int32_t fd, std::uint32_t request, std::uint32_t in, std::uint32_t in_len, std::uint32_t out,
                           std::uint32_t out_len, std::uint32_t, void* tag) {
    if (!g_ios) return -1;
    FakeIos::Request r; r.kind = 6; r.tag = tag; r.buffer = in; r.fd = fd; r.a = request; r.b = in_len; r.c = out; r.d = out_len;
    g_ios->queue.push_back(r);
    return 0;
}
void IosWait(rt_context*) {
    if (g_ios) g_ios->complete_one();
}
std::int32_t IosOpenSync(const char* path, std::uint32_t mode) {
    return std::string(path) == "/dev/fs" && mode == 0 ? 9 : -6;
}
std::int32_t FsCloseOk(std::int32_t fd) {
    return fd == 9 ? 0 : -4;
}

// Game memory below 4 GiB for one FS test: the state block first, then
// scratch buffers. Every pointer the engine touches must live down here:
// it addresses memory through 32-bit fields, so a stack buffer would
// truncate.
struct FsMemory {
    rt_fs_state* st = nullptr;
    std::uint8_t* path = nullptr;
    std::uint8_t* data = nullptr;
    std::uint32_t* out = nullptr;
    rtfs_attr_block* attr = nullptr;
    bool ok = false;
    FsMemory() {
        std::uint8_t* low = LowBuffer(sizeof(rt_fs_state) + 0x8000);
        if (!low) return;
        std::uint8_t* p = low + ((sizeof(rt_fs_state) + 31) & ~std::size_t(31));
        st = reinterpret_cast<rt_fs_state*>(low);
        std::memset(st, 0, sizeof(*st));
        path = p;
        data = p + 0x1000;
        out = reinterpret_cast<std::uint32_t*>(p + 0x2000);
        attr = reinterpret_cast<rtfs_attr_block*>(p + 0x3000);
        std::memset(p, 0, 0x4000);
        ok = true;
    }
};

// The rtfs test fixture's card: the save directory at cluster 3 holding
// SAVE.BIN, two clusters of known bytes.
// `marker`: the loader's hidden clone marker "riftwii.cln" is in the folder.
void FsFillCard(FsCard& card, fatimg::Bytes& content, rtfat_volume& volume, std::uint8_t seed, bool marker = false) {
    const std::uint32_t kClusterBytes = card.image.cluster_bytes();
    content.assign(kClusterBytes + 37, 0);
    for (std::size_t i = 0; i < content.size(); ++i) content[i] = static_cast<std::uint8_t>(i * seed + 3);
    card.image.write_data({10, 11}, content);
    fatimg::Bytes dir;
    fatimg::Bytes save = fatimg::short_entry("SAVE    BIN", 0x20, 10, static_cast<std::uint32_t>(content.size()), 0x18);
    dir.insert(dir.end(), save.begin(), save.end());
    if (marker) {
        fatimg::Bytes m = fatimg::short_entry("RIFTWII CLN", 0x22, 0, 0);
        dir.insert(dir.end(), m.begin(), m.end());
    }
    EXPECT_TRUE(card.image.write_dir({3}, dir));
    // Link the save folder into the root as SAVE so the independent
    // host reader can cross-check the card image (the engine addresses
    // dir_cluster directly and never looks at the root).
    fatimg::Bytes root = fatimg::short_entry("SAVE       ", 0x10, 3, 0);
    EXPECT_TRUE(card.image.write_dir({2}, root));
    volume = rtfat_volume{};
    volume.sectors_per_cluster = card.image.spc;
    volume.fat_lba = card.image.reserved;
    volume.fat_count = card.image.fats;
    volume.fat_sectors = card.image.fat_sectors;
    volume.data_lba = static_cast<std::uint32_t>(card.image.data_start_sector());
    volume.cluster_count = card.image.clusters;
    volume.dir_cluster = 3;
    volume.alloc_hint = 13;
}

}  // namespace

static void TestFsSyncIntercept() {
    FsMemory mem;
    if (!mem.ok) {
        std::cerr << "note: no 32-bit addressable buffer on this host, skipping the FS intercept drive" << std::endl;
        return;
    }
    FsCard card;
    fatimg::Bytes content;
    rtfat_volume volume;
    FsFillCard(card, content, volume, 7);
    rt_fs_state* st = mem.st;
    std::uint8_t* path = mem.path;
    std::uint8_t* data = mem.data;
    std::uint32_t* out = mem.out;
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

    // An ISFS ioctl on a real fd with the fs fd unknown: a short buffer is
    // not ours (replayed); a CreateFile block naming a file in the folder
    // is, and runs against the card.
    args[0] = 5; args[1] = 0x09; args[2] = FsAddr(path); args[3] = 64; args[4] = 0; args[5] = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 0);
    rtfs_attr_block* attr = mem.attr;
    std::memset(attr, 0, sizeof(*attr));
    FsCopyPath(reinterpret_cast<std::uint8_t*>(attr->filepath), prefix + "/made.bin");
    attr->ownerperm = 3;
    args[0] = 5; args[1] = 0x09; args[2] = FsAddr(attr); args[3] = sizeof(*attr); args[4] = 0; args[5] = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), RTFAT_EEXIST);

    // Async forms are taken over too (TestFsAsyncIntercept): a bad mode
    // completes immediately, but with no round-trip fake in this test the
    // deferred delivery cannot issue, so the engine fails closed with
    // -114 and no callback; callbacks are never inline.
    FsCopyPath(path, prefix + "/save.bin");
    args[0] = FsAddr(path); args[1] = 0; args[2] = 0x80001000; args[3] = 0x80002000;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), RTFAT_EIO);
    EXPECT_EQ(st->inline_deliveries, 0u);
    EXPECT_EQ(st->dead, 1u);
    EXPECT_EQ(st->fs.busy, 0u);
    st->dead = 0;

    // Flag off: replay. No state block: replay.
    ctx.flags = 0;
    args[0] = FsAddr(path); args[1] = 3;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 0);
    ctx.flags = RT_FLAG_FS;
    ctx.fs_state = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 0);
    ctx.fs_state = FsAddr(st);

    // An engine that never comes free (nothing drives it here): the sync
    // arrival waits its bound, then answers -114; the engine untouched.
    st->fs.busy = 1;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), RTFAT_EIO);
    EXPECT_EQ(st->waits, 1u);
    EXPECT_EQ(st->wait_timeouts, 1u);
    EXPECT_EQ(st->fs.busy, 1u);
    st->fs.busy = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
    EXPECT_TRUE(static_cast<std::int32_t>(result) >= static_cast<std::int32_t>(RTFS_FD_BASE));
    args[0] = result;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(2), args, &result), 1);

    // The game's synchronous open of /dev/fs: done through the original
    // (call-through), the fd learned; without the original it replays.
    FsCopyPath(path, "/dev/fs");
    args[0] = FsAddr(path); args[1] = 0;
    st->open_sync = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 0);
    EXPECT_EQ(st->fs.fs_fd, -1);
    st->open_sync = 0x80100000u;
    rt_host_fs_open_sync = IosOpenSync;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
    EXPECT_EQ(result, 9u);
    EXPECT_EQ(st->fs.fs_fd, 9);
    EXPECT_EQ(st->fs_fd_learned, 1u);
    args[1] = 1;  // a mode the stand-in refuses: the failure is the game's, nothing learned anew
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), -6);
    EXPECT_EQ(st->fs.fs_fd, 9);
    // A sync close of the learned fd calls its original explicitly and
    // forgets it; without the original it replays, learning nothing new.
    st->close_sync = 0x80100001u;
    rt_host_fs_close_sync = FsCloseOk;
    args[0] = 9;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(2), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(st->fs.fs_fd, -1);
    st->close_sync = 0;
    rt_host_fs_close_sync = nullptr;
    rt_host_fs_open_sync = nullptr;

    // A dead engine answers -114 without waiting or touching the card.
    st->dead = 1;
    const std::uint32_t transfers_dead = card.transfers;
    FsCopyPath(path, prefix + "/save.bin");
    args[0] = FsAddr(path); args[1] = 3;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), RTFAT_EIO);
    EXPECT_EQ(card.transfers, transfers_dead);
    EXPECT_EQ(st->waits, 1u);
    st->dead = 0;

    // A transfer the card refuses: the request fails with -114, the engine
    // is released, and the next request runs.
    rt_host_fs_transfer = nullptr;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), RTFAT_EIO);
    EXPECT_EQ(st->fs.busy, 0u);
    EXPECT_EQ(st->dead, 0u);
    EXPECT_EQ(st->failures, 1u);
    rt_host_fs_transfer = FsTransfer;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
    EXPECT_TRUE(static_cast<std::int32_t>(result) >= static_cast<std::int32_t>(RTFS_FD_BASE));
    args[0] = result;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(2), args, &result), 1);

    rt_host_fs_transfer = nullptr;
    g_fs_card = nullptr;
}

// ---- asynchronous savegame interception (slice 4B3) ------------------------

static void TestFsAsyncIntercept() {
    FsMemory mem;
    if (!mem.ok) {
        std::cerr << "note: no 32-bit addressable buffer on this host, skipping the async FS drive" << std::endl;
        return;
    }
    FsCard card;
    fatimg::Bytes content;
    rtfat_volume volume;
    FsFillCard(card, content, volume, 5);
    rt_fs_state* st = mem.st;
    std::uint8_t* path = mem.path;
    std::uint8_t* data = mem.data;
    st->complete_fs = 0x935D0200;
    const std::string prefix = "/title/00010000/524d4345/data";
    EXPECT_EQ(rtfs_init(&st->fs, &volume, prefix.c_str(), -1), RTFAT_OK);

    rt_context ctx{};
    ctx.magic = RT_CONTEXT_MAGIC;
    ctx.flags = RT_FLAG_FS;
    ctx.fs_state = FsAddr(st);
    FakeIos ios;
    ios.card = &card;
    ios.ctx = &ctx;
    g_ios = &ios;
    g_fs_card = &card;
    rt_host_fs_transfer = FsTransfer;
    rt_host_fs_issue = IosIssue;
    rt_host_fs_defer = IosDefer;
    rt_host_fs_wait = IosWait;
    rt_host_fs_open_async = IosOpenAsync;
    rt_host_fs_close_async = IosCloseAsync;
    st->open_async = 0x80100000u;
    st->close_async = 0x80100001u;
    CbLog log;
    g_cb_log = &log;
    rt_host_game_callback = GameCb;

    std::uintptr_t args[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::uint32_t result = 0xDEADu;
    std::int32_t ipc_result = 0;
    std::uintptr_t cb = 0xDEADu, ud = 0xDEADu;

    // Async open: accepted (0), a card transfer issued, nothing delivered
    // until IOS completes it; the game's callback then gets the fd.
    FsCopyPath(path, prefix + "/save.bin");
    args[0] = FsAddr(path); args[1] = 3; args[2] = 0x80001000; args[3] = 0x80002000;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(args[2], 0x80001000u);  // the game's registers are not touched: the call is answered, not replayed
    EXPECT_EQ(ios.queue.size(), std::size_t(1));
    EXPECT_EQ(ios.queue[0].kind, 1);
    EXPECT_EQ(ios.queue[0].tag, static_cast<void*>(&st->pend));
    EXPECT_EQ(st->pend.in_use, 1u);
    EXPECT_TRUE(ios.delivered.empty());
    EXPECT_EQ(log.calls, 0u);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].cb, 0x80001000u);
    EXPECT_EQ(ios.delivered[0].ud, 0x80002000u);
    EXPECT_TRUE(ios.delivered[0].result >= static_cast<std::int32_t>(RTFS_FD_BASE));
    EXPECT_EQ(st->pend.in_use, 0u);
    EXPECT_EQ(st->fs.busy, 0u);
    EXPECT_TRUE(st->transfers >= 1u);
    const std::uint32_t fd = static_cast<std::uint32_t>(ios.delivered[0].result);
    ios.delivered.clear();

    // Async read: bytes land from the card, the count delivered.
    std::memset(data, 0xEE, 512);
    args[0] = fd; args[1] = FsAddr(data); args[2] = 64; args[3] = 0x80001001; args[4] = 0x80002001;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(3), args, &result), 1);
    EXPECT_EQ(result, 0u);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].result, 64);
    EXPECT_EQ(ios.delivered[0].cb, 0x80001001u);
    EXPECT_TRUE(std::memcmp(data, content.data(), 64) == 0);
    ios.delivered.clear();

    // Async seek completes without I/O: the result rides a null round trip
    // and reaches the callback only when IOS completes that, never inline.
    args[0] = fd; args[1] = 0; args[2] = 0; args[3] = 0x80001002; args[4] = 0x80002002;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(5), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(log.calls, 0u);
    EXPECT_EQ(ios.queue.size(), std::size_t(1));
    EXPECT_EQ(ios.queue[0].kind, 2);
    EXPECT_TRUE(ios.delivered.empty());
    EXPECT_EQ(st->deferred, 1u);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].cb, 0x80001002u);
    EXPECT_EQ(ios.delivered[0].ud, 0x80002002u);
    EXPECT_EQ(ios.delivered[0].result, 0);
    ios.delivered.clear();

    // Async write, seek home, async read-back.
    for (int i = 0; i < 16; ++i) data[i] = static_cast<std::uint8_t>(0x70 + i);
    args[0] = fd; args[1] = FsAddr(data); args[2] = 16; args[3] = 0x80001003; args[4] = 0x80002003;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(4), args, &result), 1);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].result, 16);
    EXPECT_EQ(ios.delivered[0].cb, 0x80001003u);
    ios.delivered.clear();
    args[0] = fd; args[1] = 0; args[2] = 0; args[3] = 0x80001004; args[4] = 0x80002004;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(5), args, &result), 1);
    ios.drain();
    std::memset(data, 0, 16);
    args[0] = fd; args[1] = FsAddr(data); args[2] = 16; args[3] = 0x80001005; args[4] = 0x80002005;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(3), args, &result), 1);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(2));
    EXPECT_EQ(ios.delivered[1].result, 16);
    for (int i = 0; i < 16; ++i) EXPECT_EQ(data[i], static_cast<std::uint8_t>(0x70 + i));
    ios.delivered.clear();

    // Async close: deferred as well.
    args[0] = fd; args[1] = 0x80001006; args[2] = 0x80002006;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(2), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_TRUE(ios.delivered.empty());
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].cb, 0x80001006u);
    EXPECT_EQ(ios.delivered[0].result, 0);
    ios.delivered.clear();

    // Async open of a missing file needs the lookup: -106 at completion,
    // never a replay to NAND.
    FsCopyPath(path, prefix + "/gone.bin");
    args[0] = FsAddr(path); args[1] = 3; args[2] = 0x80001007; args[3] = 0x80002007;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
    EXPECT_EQ(result, 0u);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].result, RTFAT_ENOENT);
    EXPECT_EQ(ios.delivered[0].cb, 0x80001007u);
    ios.delivered.clear();

    // A null game callback: answered (0), nothing to deliver, no round trip.
    const std::uint32_t deferred0 = st->deferred;
    args[0] = FsAddr(path); args[1] = 0; args[2] = 0; args[3] = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_TRUE(ios.queue.empty());
    EXPECT_EQ(st->deferred, deferred0);
    EXPECT_EQ(log.calls, 0u);

    // Device opens call the game's original explicitly under observation:
    // accepted at the call with registers untouched, the queued NAND open
    // completes through our entry, which learns the fd and delivers.
    FsCopyPath(path, "/dev/fs");
    args[0] = FsAddr(path); args[1] = 0; args[2] = 0x80001008; args[3] = 0x80002008;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(args[2], 0x80001008u);
    EXPECT_EQ(args[3], 0x80002008u);
    EXPECT_EQ(ios.queue.size(), std::size_t(1));
    EXPECT_EQ(ios.queue[0].kind, 3);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].result, 9);
    EXPECT_EQ(ios.delivered[0].cb, 0x80001008u);
    EXPECT_EQ(ios.delivered[0].ud, 0x80002008u);
    EXPECT_EQ(st->fs.fs_fd, 9);
    EXPECT_EQ(st->fs_fd_learned, 1u);
    ios.delivered.clear();
    // Two device opens held uncompleted take both snoop slots; a third
    // finds them taken and replays unobserved, its callback pair
    // untouched. Each completion then delivers to its own game callback.
    args[0] = FsAddr(path); args[1] = 0; args[2] = 0x80001020; args[3] = 0x80002020;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
    EXPECT_EQ(result, 0u);
    args[0] = FsAddr(path); args[1] = 0; args[2] = 0x80001021; args[3] = 0x80002021;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(ios.queue.size(), std::size_t(2));
    EXPECT_TRUE(ios.queue[0].tag != ios.queue[1].tag);
    args[0] = FsAddr(path); args[1] = 0; args[2] = 0x80001022; args[3] = 0x80002022;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 0);
    EXPECT_EQ(args[2], 0x80001022u);
    EXPECT_EQ(args[3], 0x80002022u);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(2));
    EXPECT_EQ(ios.delivered[0].cb, 0x80001020u);
    EXPECT_EQ(ios.delivered[1].cb, 0x80001021u);
    EXPECT_EQ(st->fs.fs_fd, 9);
    ios.delivered.clear();
    // An ISFS ioctl on the learned fd is ours: full takeover cycle.
    FsCopyPath(path, prefix + "/save.bin");
    args[0] = 9; args[1] = 0x06; args[2] = FsAddr(path); args[3] = 64;
    args[4] = FsAddr(mem.attr); args[5] = static_cast<std::uint32_t>(sizeof(rtfs_attr_block));
    args[6] = 0x8000100D; args[7] = 0x8000200D;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(6), args, &result), 1);
    EXPECT_EQ(result, 0u);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].result, 0);
    EXPECT_EQ(ios.delivered[0].cb, 0x8000100Du);
    EXPECT_EQ(mem.attr->ownerperm, RTFS_META_OWNER_PERM);
    ios.delivered.clear();
    // With the fd learned, the same ioctl on another real fd replays; the
    // device close calls its original explicitly, and once forgotten the
    // same ioctl is ours again by path.
    args[0] = 5; args[1] = 0x06; args[2] = FsAddr(path); args[3] = 64;
    args[4] = FsAddr(mem.attr); args[5] = static_cast<std::uint32_t>(sizeof(rtfs_attr_block));
    args[6] = 0x8000100E; args[7] = 0x8000200E;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(6), args, &result), 0);
    args[0] = 9; args[1] = 0x80001009; args[2] = 0x80002009;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(2), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(ios.queue.size(), std::size_t(1));
    EXPECT_EQ(ios.queue[0].kind, 4);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].cb, 0x80001009u);
    EXPECT_EQ(st->fs.fs_fd, -1);
    ios.delivered.clear();
    args[0] = 5; args[1] = 0x06; args[2] = FsAddr(path); args[3] = 64;
    args[4] = FsAddr(mem.attr); args[5] = static_cast<std::uint32_t>(sizeof(rtfs_attr_block));
    args[6] = 0x8000100F; args[7] = 0x8000200F;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(6), args, &result), 1);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].cb, 0x8000100Fu);
    ios.delivered.clear();

    // Arrivals behind a busy engine queue, and start by themselves from the
    // completion that frees it; deliveries keep the order of arrival.
    FsCopyPath(path, prefix + "/save.bin");
    args[0] = FsAddr(path); args[1] = 3; args[2] = 0x80001010; args[3] = 0x80002010;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
    EXPECT_EQ(st->fs.busy, 1u);
    args[2] = 0x80001011; args[3] = 0x80002011;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(st->queued, 1u);
    EXPECT_EQ(st->queue_count, 1u);
    EXPECT_EQ(ios.queue.size(), std::size_t(1));  // only the first one's transfer is in flight
    ios.drain();
    EXPECT_EQ(st->queue_count, 0u);
    EXPECT_EQ(ios.delivered.size(), std::size_t(2));
    EXPECT_EQ(ios.delivered[0].cb, 0x80001010u);
    EXPECT_EQ(ios.delivered[1].cb, 0x80001011u);
    EXPECT_TRUE(ios.delivered[0].result >= static_cast<std::int32_t>(RTFS_FD_BASE));
    EXPECT_TRUE(ios.delivered[1].result >= static_cast<std::int32_t>(RTFS_FD_BASE));
    EXPECT_TRUE(ios.delivered[0].result != ios.delivered[1].result);
    const std::uint32_t fd_a = static_cast<std::uint32_t>(ios.delivered[0].result);
    const std::uint32_t fd_b = static_cast<std::uint32_t>(ios.delivered[1].result);
    ios.delivered.clear();
    // A queued request that completes without I/O is deferred like any other.
    args[0] = fd_a; args[1] = FsAddr(data); args[2] = 8; args[3] = 0x80001012; args[4] = 0x80002012;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(3), args, &result), 1);
    args[0] = fd_b; args[1] = 3; args[2] = 0; args[3] = 0x80001013; args[4] = 0x80002013;  // seek, queued
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(5), args, &result), 1);
    EXPECT_EQ(st->queue_count, 1u);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(2));
    EXPECT_EQ(ios.delivered[0].cb, 0x80001012u);
    EXPECT_EQ(ios.delivered[0].result, 8);
    EXPECT_EQ(ios.delivered[1].cb, 0x80001013u);
    EXPECT_EQ(ios.delivered[1].result, 3);
    ios.delivered.clear();
    // The queue is RT_FS_QUEUE deep; one more is refused at the call, with
    // no callback to follow.
    args[0] = fd_a; args[1] = FsAddr(data); args[2] = 8; args[3] = 0x80001014; args[4] = 0x80002014;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(3), args, &result), 1);
    for (std::uint32_t i = 0; i < RT_FS_QUEUE; ++i) {
        args[3] = 0x80001020 + i; args[4] = 0x80002020 + i;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(3), args, &result), 1);
        EXPECT_EQ(result, 0u);
    }
    EXPECT_EQ(st->queue_count, RT_FS_QUEUE);
    args[3] = 0x80001030; args[4] = 0x80002030;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(3), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), RTFAT_EACCESS);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1 + RT_FS_QUEUE));
    EXPECT_EQ(ios.delivered[0].cb, 0x80001014u);
    EXPECT_EQ(ios.delivered[RT_FS_QUEUE].cb, 0x80001020u + RT_FS_QUEUE - 1);
    for (const FakeIos::Delivery& d : ios.delivered) EXPECT_EQ(d.result, 8);
    ios.delivered.clear();

    // A synchronous arrival while an async request is in flight waits for
    // the engine (the IPC interrupt drives the request ahead) and then
    // runs; both are answered, the async one first.
    for (int i = 0; i < 16; ++i) data[i] = static_cast<std::uint8_t>(0xA0 + i);
    args[0] = fd_a; args[1] = 0; args[2] = 0; args[3] = 0; args[4] = 0;  // sync seek home
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(5), args, &result), 1);
    args[0] = fd_a; args[1] = FsAddr(data); args[2] = 16; args[3] = 0x80001040; args[4] = 0x80002040;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(4), args, &result), 1);  // async write, in flight
    EXPECT_EQ(st->fs.busy, 1u);
    args[0] = fd_a; args[1] = 0; args[2] = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(5), args, &result), 1);  // sync seek home: waits
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(st->waits, 1u);
    EXPECT_EQ(st->wait_timeouts, 0u);
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].cb, 0x80001040u);
    EXPECT_EQ(ios.delivered[0].result, 16);
    ios.delivered.clear();
    std::memset(data + 256, 0, 16);
    args[0] = fd_a; args[1] = FsAddr(data + 256); args[2] = 16;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(3), args, &result), 1);  // sync read sees the async write
    EXPECT_EQ(result, 16u);
    for (int i = 0; i < 16; ++i) EXPECT_EQ(data[256 + i], static_cast<std::uint8_t>(0xA0 + i));

    // A transfer IOS refuses to issue: the request fails with -114 (deferred
    // delivery), the engine is released, later requests run.
    ios.refuse_issues = 1;
    FsCopyPath(path, prefix + "/save.bin");
    args[0] = FsAddr(path); args[1] = 1; args[2] = 0x80001050; args[3] = 0x80002050;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(st->fs.busy, 0u);
    EXPECT_EQ(st->dead, 0u);
    EXPECT_EQ(ios.queue.size(), std::size_t(1));
    EXPECT_EQ(ios.queue[0].kind, 2);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].cb, 0x80001050u);
    EXPECT_EQ(ios.delivered[0].result, RTFAT_EIO);
    ios.delivered.clear();
    // A transfer that fails at completion: -114 by tail call.
    ios.fail_transfers = 1;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].result, RTFAT_EIO);
    EXPECT_EQ(st->fs.busy, 0u);
    ios.delivered.clear();
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_TRUE(ios.delivered[0].result >= static_cast<std::int32_t>(RTFS_FD_BASE));
    const std::uint32_t fd_c = static_cast<std::uint32_t>(ios.delivered[0].result);
    ios.delivered.clear();

    // No null round trip possible (every retry refused): no callback is
    // made and the engine fails closed with -114 instead; callbacks are
    // never inline.
    ios.refuse_defers = 3;
    args[0] = fd_c; args[1] = 0; args[2] = 0; args[3] = 0x80001060; args[4] = 0x80002060;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(5), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), RTFAT_EIO);
    EXPECT_EQ(log.calls, 0u);
    EXPECT_EQ(st->inline_deliveries, 0u);
    EXPECT_EQ(st->dead, 1u);
    EXPECT_TRUE(ios.queue.empty());
    EXPECT_TRUE(ios.delivered.empty());
    ios.refuse_defers = 0;
    st->dead = 0;

    // A dead engine answers -114 (deferred) without touching the card.
    st->dead = 1;
    const std::uint32_t transfers_dead = card.transfers;
    args[0] = fd_c; args[1] = FsAddr(data); args[2] = 8; args[3] = 0x80001061; args[4] = 0x80002061;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(3), args, &result), 1);
    EXPECT_EQ(result, 0u);
    ios.drain();
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered[0].result, RTFAT_EIO);
    EXPECT_EQ(card.transfers, transfers_dead);
    st->dead = 0;
    ios.delivered.clear();

    // Unknown completion tags are swallowed, never tail-called.
    cb = 0xDEADu; ud = 0xDEADu; ipc_result = 42;
    int bogus = 0;
    rt_on_fs_complete(&ctx, &ipc_result, &bogus, &cb, &ud);
    EXPECT_EQ(cb, 0u);

    // Flag off: replay, and completions do nothing.
    ctx.flags = 0;
    args[0] = fd_c; args[1] = 0; args[2] = 0; args[3] = 0x80001070; args[4] = 0x80002070;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(5), args, &result), 0);
    cb = 0xDEADu;
    rt_on_fs_complete(&ctx, &ipc_result, &st->pend, &cb, &ud);
    EXPECT_EQ(cb, 0u);
    ctx.flags = RT_FLAG_FS;

    rt_host_game_callback = nullptr;
    rt_host_fs_transfer = nullptr;
    rt_host_fs_issue = nullptr;
    rt_host_fs_defer = nullptr;
    rt_host_fs_wait = nullptr;
    g_cb_log = nullptr;
    g_fs_card = nullptr;
    g_ios = nullptr;
}

// ---- the rename import (slice 5) ------------------------------------------

namespace {

// The console's NAND as an import sees it: one file, opened for reading
// through the game's synchronous IOS_Open (fd 20), read in pieces, its
// stats answered on that fd, deleted through the /dev/fs fd (9).
struct FakeNand {
    std::string path;
    std::vector<std::uint8_t> bytes;
    bool open = false;
    std::size_t pos = 0;
    std::uint32_t opens = 0, closes = 0, reads = 0;
    std::vector<std::string> deleted;
    long read_error_after = -1;  // >= 0: a read starting at or past this offset fails with -114
    // A directory for the clone: its path and its entries (an entry with
    // no bytes stands for a subdirectory, which no open accepts).
    std::string dir_prefix;
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> dir;
    std::uint32_t readdirs = 0, fs_opens = 0, fs_closes = 0;
};
FakeNand* g_nand = nullptr;
constexpr std::int32_t kNandFd = 20;
std::int32_t NandOpenSync(const char* path, std::uint32_t mode) {
    if (std::string(path) == "/dev/fs" && mode == 0) {
        if (g_nand) g_nand->fs_opens++;
        return 9;
    }
    if (!g_nand) return RTFAT_ENOENT;
    if (!g_nand->dir_prefix.empty() && std::string(path) != g_nand->path) {
        // A file of the directory becomes the one file the fake serves.
        const std::string p(path);
        for (const auto& e : g_nand->dir) {
            if (p == g_nand->dir_prefix + "/" + e.first && !e.second.empty()) {
                g_nand->path = p;
                g_nand->bytes = e.second;
            }
        }
    }
    if (std::string(path) != g_nand->path || mode != 1 || g_nand->open) return RTFAT_ENOENT;
    g_nand->open = true;
    g_nand->pos = 0;
    g_nand->opens++;
    return kNandFd;
}
// ISFS ReadDir on the /dev/fs fd, both forms: (path) -> count; (path,
// count) -> the names packed one after another, count.
std::int32_t NandIoctlvSync(std::int32_t fd, std::uint32_t request, std::uint32_t in_count, std::uint32_t out_count,
                            rt_ioctlv* vec) {
    if (!g_nand || fd != 9 || request != RTFS_IOCTL_READDIR) return -4;
    g_nand->readdirs++;
    const std::string p(reinterpret_cast<const char*>(static_cast<std::uintptr_t>(vec[0].data)));
    if (p != g_nand->dir_prefix) return RTFAT_ENOENT;
    if (in_count == 1 && out_count == 1) {
        *reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(vec[1].data)) = static_cast<std::uint32_t>(g_nand->dir.size());
        return 0;
    }
    if (in_count != 2 || out_count != 2) return RTFAT_EINVAL;
    const std::uint32_t want = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(vec[1].data));
    auto* names = reinterpret_cast<char*>(static_cast<std::uintptr_t>(vec[2].data));
    // Packed as Dolphin's IOS packs them: 13 bytes cleared, the name, a
    // NUL at the 13th byte, then the next name right after this one's NUL.
    std::uint32_t n = 0, at = 0;
    for (const auto& e : g_nand->dir) {
        if (n >= want || vec[2].len < at + 13) break;
        std::memset(names + at, 0, 13);
        std::memcpy(names + at, e.first.c_str(), e.first.size());
        names[at + 12] = 0;
        at += static_cast<std::uint32_t>(e.first.size()) + 1;
        ++n;
    }
    *reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(vec[3].data)) = n;
    return 0;
}
std::int32_t NandCloseSync(std::int32_t fd) {
    if (fd == 9) {
        // Like NandOpenSync's /dev/fs answer: closable without NAND state.
        if (g_nand) g_nand->fs_closes++;
        return 0;
    }
    if (!g_nand || fd != kNandFd || !g_nand->open) return -4;
    g_nand->open = false;
    g_nand->closes++;
    return 0;
}
std::int32_t NandReadSync(std::int32_t fd, std::uint32_t buffer, std::uint32_t length) {
    if (!g_nand || fd != kNandFd || !g_nand->open) return -4;
    g_nand->reads++;
    if (g_nand->read_error_after >= 0 && g_nand->pos >= static_cast<std::size_t>(g_nand->read_error_after)) return RTFAT_EIO;
    const std::size_t n = std::min<std::size_t>(length, g_nand->bytes.size() - g_nand->pos);
    std::memcpy(reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(buffer)), g_nand->bytes.data() + g_nand->pos, n);
    g_nand->pos += n;
    return static_cast<std::int32_t>(n);
}
std::int32_t NandIoctlSync(std::int32_t fd, std::uint32_t request, std::uint32_t in, std::uint32_t in_len, std::uint32_t out,
                           std::uint32_t out_len) {
    if (!g_nand) return -4;
    if (fd == kNandFd && request == RTFS_IOCTL_GETFILESTATS && out_len >= 8) {
        auto* o = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(out));
        o[0] = static_cast<std::uint32_t>(g_nand->bytes.size());
        o[1] = static_cast<std::uint32_t>(g_nand->pos);
        return 0;
    }
    if (fd == 9 && request == RTFS_IOCTL_DELETE && in_len == RTFS_PATH_BYTES) {
        const std::string p(reinterpret_cast<const char*>(static_cast<std::uintptr_t>(in)));
        g_nand->deleted.push_back(p);
        if (p != g_nand->path || g_nand->open) return RTFAT_ENOENT;
        g_nand->path.clear();
        g_nand->bytes.clear();
        return 0;
    }
    return -4;
}
void NandFill(FakeNand& nand, const std::string& path, std::size_t size, std::uint8_t seed) {
    nand.path = path;
    nand.bytes.assign(size, 0);
    for (std::size_t i = 0; i < size; ++i) nand.bytes[i] = static_cast<std::uint8_t>(i * seed + 11);
}

}  // namespace

static void TestFsRenameImport() {
    FsMemory mem;
    if (!mem.ok) {
        std::cerr << "note: no 32-bit addressable buffer on this host, skipping the FS import drive" << std::endl;
        return;
    }
    FsCard card;
    fatimg::Bytes content;
    rtfat_volume volume;
    FsFillCard(card, content, volume, 3);
    rt_fs_state* st = mem.st;
    std::uint8_t* path = mem.path;
    std::uint8_t* data = mem.data;
    std::uint32_t* out = mem.out;
    const std::string prefix = "/title/00010004/524d4345/data";
    EXPECT_EQ(rtfs_init(&st->fs, &volume, prefix.c_str(), -1), RTFAT_OK);
    st->open_sync = st->close_sync = st->read_sync = st->ioctl_sync = 0x80100000u;

    rt_context ctx{};
    ctx.magic = RT_CONTEXT_MAGIC;
    ctx.flags = RT_FLAG_FS;
    ctx.fs_state = FsAddr(st);
    g_fs_card = &card;
    rt_host_fs_transfer = FsTransfer;
    rt_host_fs_open_sync = NandOpenSync;
    rt_host_fs_close_sync = NandCloseSync;
    rt_host_fs_read_sync = NandReadSync;
    rt_host_fs_ioctl_sync = NandIoctlSync;
    FakeNand nand;
    g_nand = &nand;

    std::uintptr_t args[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::uint32_t result = 0xDEADu;
    const auto rename_args = [&](const std::string& from, const std::string& to) {
        FsCopyPath(path, from);
        FsCopyPath(path + RTFS_PATH_BYTES, to);
        args[0] = 9; args[1] = RTFS_IOCTL_RENAME; args[2] = FsAddr(path); args[3] = 2 * RTFS_PATH_BYTES; args[4] = 0; args[5] = 0;
        args[6] = 0x80001000u; args[7] = 0x80002000u;
    };
    // Opens a card file for reading and checks its size and a window of
    // its bytes against `expect`, through the dispatcher.
    const auto check_card_file = [&](const std::string& name, const std::vector<std::uint8_t>& expect, std::uint32_t at) {
        FsCopyPath(path, prefix + "/" + name);
        args[0] = FsAddr(path); args[1] = 1;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
        EXPECT_TRUE(static_cast<std::int32_t>(result) >= static_cast<std::int32_t>(RTFS_FD_BASE));
        const std::uint32_t fd = result;
        args[0] = fd; args[1] = RTFS_IOCTL_GETFILESTATS; args[2] = 0; args[3] = 0; args[4] = FsAddr(out); args[5] = 8;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 1);
        EXPECT_EQ(result, 0u);
        EXPECT_EQ(out[0], static_cast<std::uint32_t>(expect.size()));
        args[0] = fd; args[1] = at; args[2] = 0;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(5), args, &result), 1);
        EXPECT_EQ(result, at);
        const std::uint32_t n = std::min<std::uint32_t>(64, static_cast<std::uint32_t>(expect.size()) - at);
        std::memset(data, 0xEE, 64);
        args[0] = fd; args[1] = FsAddr(data); args[2] = n;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(3), args, &result), 1);
        EXPECT_EQ(result, n);
        EXPECT_TRUE(std::memcmp(data, expect.data() + at, n) == 0);
        args[0] = fd;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(2), args, &result), 1);
        EXPECT_EQ(result, 0u);
    };
    const auto card_has = [&](const std::string& name) {
        FsCopyPath(path, prefix + "/" + name);
        args[0] = FsAddr(path); args[1] = 1;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
        if (static_cast<std::int32_t>(result) < 0) return false;
        args[0] = result;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(2), args, &result), 1);
        return true;
    };

    // The SDK's safe write: a file under /tmp renamed into the folder. Two
    // full pieces and a tail come off NAND (three reads), land on the card
    // byte for byte, and the source is deleted; the fd the rename came on
    // is learned as the /dev/fs fd.
    NandFill(nand, "/tmp/rksys.dat", 2 * RT_FS_IMPORT_BYTES + 1234, 5);
    const std::vector<std::uint8_t> rksys = nand.bytes;
    rename_args("/tmp/rksys.dat", prefix + "/rksys.dat");
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(st->imports, 1u);
    EXPECT_EQ(st->import_failures, 0u);
    EXPECT_EQ(nand.opens, 1u);
    EXPECT_EQ(nand.closes, 1u);
    EXPECT_EQ(nand.reads, 3u);
    EXPECT_EQ(nand.deleted.size(), std::size_t(1));
    EXPECT_TRUE(nand.deleted.back() == "/tmp/rksys.dat");
    EXPECT_TRUE(nand.path.empty());
    EXPECT_EQ(st->fs.fs_fd, 9);
    EXPECT_EQ(st->fs.busy, 0u);
    EXPECT_EQ(ctx.fs_hijacked, 1u);
    check_card_file("rksys.dat", rksys, 0);
    check_card_file("rksys.dat", rksys, RT_FS_IMPORT_BYTES - 16);
    check_card_file("rksys.dat", rksys, 2 * RT_FS_IMPORT_BYTES + 1234 - 50);
    ExpectNoStage(card);

    // Renaming onto the file replaces it, size and all.
    NandFill(nand, "/tmp/rksys.dat", 700, 7);
    const std::vector<std::uint8_t> rksys2 = nand.bytes;
    rename_args("/tmp/rksys.dat", prefix + "/rksys.dat");
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(st->imports, 2u);
    EXPECT_EQ(nand.reads, 4u);
    check_card_file("rksys.dat", rksys2, 0);
    check_card_file("rksys.dat", rksys2, 640);

    // Out of the folder: refused, the file stays.
    rename_args(prefix + "/rksys.dat", "/tmp/rksys.dat");
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), RTFAT_EACCESS);
    EXPECT_EQ(st->import_refused, 1u);
    EXPECT_TRUE(card_has("rksys.dat"));

    // A source NAND lacks: its open's answer is the rename's, nothing made.
    NandFill(nand, "/tmp/other.bin", 10, 1);
    rename_args("/tmp/none.bin", prefix + "/none.bin");
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), RTFAT_ENOENT);
    EXPECT_EQ(st->imports, 3u);
    EXPECT_EQ(st->import_failures, 1u);
    EXPECT_FALSE(card_has("none.bin"));

    // A read that fails midway: the destination is removed again, the
    // source closed and left on NAND, the engine free.
    NandFill(nand, "/tmp/big.bin", RT_FS_IMPORT_BYTES + 100, 9);
    nand.read_error_after = static_cast<long>(RT_FS_IMPORT_BYTES);
    rename_args("/tmp/big.bin", prefix + "/big.bin");
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), RTFAT_EIO);
    EXPECT_EQ(st->import_failures, 2u);
    EXPECT_FALSE(nand.open);
    EXPECT_TRUE(nand.path == "/tmp/big.bin");
    EXPECT_FALSE(card_has("big.bin"));
    EXPECT_EQ(st->fs.busy, 0u);
    nand.read_error_after = -1;

    // Without the game's synchronous functions there is no way to read
    // NAND: refused, the source untouched.
    st->read_sync = 0;
    rename_args("/tmp/big.bin", prefix + "/big.bin");
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 1);
    EXPECT_EQ(static_cast<std::int32_t>(result), RTFAT_EACCESS);
    EXPECT_EQ(st->import_refused, 2u);
    EXPECT_EQ(nand.opens, 3u);
    st->read_sync = 0x80100000u;

    // Neither side in the folder, or a short block: not ours, replayed.
    rename_args("/tmp/a.bin", "/tmp/b.bin");
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 0);
    rename_args("/tmp/big.bin", prefix + "/big.bin");
    args[3] = RTFS_PATH_BYTES;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 0);
    // On another real fd once the fs fd is known: not ours either.
    rename_args("/tmp/big.bin", prefix + "/big.bin");
    args[0] = 7;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 0);
    EXPECT_EQ(st->imports, 4u);

    // The async form: an asynchronous import job, from an IPC callback
    // as well as from a thread. The rename returns at once with nothing
    // done; the job runs from the completions of its own requests (NAND
    // through the game's async originals, the card through the engine),
    // and the game's callback is the tail call of the last one.
    FakeIos ios;
    ios.card = &card;
    ios.ctx = &ctx;
    g_ios = &ios;
    rt_host_fs_issue = IosIssue;
    rt_host_fs_defer = IosDefer;
    rt_host_fs_wait = IosWait;
    rt_host_fs_open_async = IosOpenAsync;
    rt_host_fs_close_async = IosCloseAsync;
    rt_host_fs_read_async = IosReadAsync;
    rt_host_fs_ioctl_async = IosIoctlAsync;
    rt_host_game_callback = GameCb;
    st->open_async = st->close_async = st->read_async = 0x80100000u;
    ctx.di_read_entry = 0x80100000u;
    CbLog log;
    g_cb_log = &log;
    rt_host_fs_in_thread = 0;
    NandFill(nand, "/tmp/banner.bin", 2 * RT_FS_IMPORT_BYTES + 1061, 13);
    const std::vector<std::uint8_t> banner = nand.bytes;
    rename_args("/tmp/banner.bin", prefix + "/banner.bin");
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(6), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(st->imports, 5u);
    EXPECT_EQ(st->import_jobs, 1u);
    EXPECT_EQ(st->import_failures, 2u);
    EXPECT_EQ(st->job.active, 1u);
    EXPECT_EQ(ios.queue.size(), std::size_t(1));  // the NAND open, nothing else yet
    EXPECT_EQ(nand.opens, 3u);
    EXPECT_EQ(st->fs.busy, 0u);
    EXPECT_EQ(log.calls, 0u);
    ios.drain();
    EXPECT_EQ(st->job.active, 0u);
    EXPECT_EQ(ios.delivered.size(), std::size_t(1));
    EXPECT_EQ(ios.delivered.back().cb, 0x80001000u);
    EXPECT_EQ(ios.delivered.back().ud, 0x80002000u);
    EXPECT_EQ(ios.delivered.back().result, 0);
    EXPECT_EQ(nand.opens, 4u);
    EXPECT_EQ(nand.closes, 4u);
    EXPECT_EQ(nand.reads, 9u);  // three pieces
    EXPECT_EQ(ios.nand_requests, 7u);  // open, stats, 3 reads, close, delete
    EXPECT_TRUE(nand.path.empty());
    EXPECT_EQ(st->import_failures, 2u);
    EXPECT_EQ(st->fs.busy, 0u);
    EXPECT_EQ(st->queue_count, 0u);
    check_card_file("banner.bin", banner, 1000);
    check_card_file("banner.bin", banner, 2 * RT_FS_IMPORT_BYTES + 1061 - 40);
    // Kirby's banner: a full piece and a partial one (61600 bytes).
    NandFill(nand, "/tmp/wibn.bin", 61600, 29);
    const std::vector<std::uint8_t> wibn = nand.bytes;
    rename_args("/tmp/wibn.bin", prefix + "/wibn.bin");
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(6), args, &result), 1);
    ios.drain();
    EXPECT_EQ(ios.delivered.back().result, 0);
    check_card_file("wibn.bin", wibn, 61600 - 40);
    check_card_file("wibn.bin", wibn, RT_FS_IMPORT_BYTES - 16);
    ExpectNoStage(card);

    // A job whose card request finds the engine held by the game's own
    // async read (issued between the job's NAND open and its stats):
    // the job's request queues and starts from the read's completion.
    // And a NAND read that fails midway: the destination is closed and
    // removed again, the source closed and left on NAND, the rename's
    // -114 delivered, the engine free.
    {
        NandFill(nand, "/tmp/big2.bin", RT_FS_IMPORT_BYTES + 100, 9);
        nand.read_error_after = static_cast<long>(RT_FS_IMPORT_BYTES);
        rename_args("/tmp/big2.bin", prefix + "/big2.bin");
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(6), args, &result), 1);
        EXPECT_EQ(result, 0u);
        EXPECT_TRUE(ios.complete_one());  // the open: the stats request follows
        EXPECT_EQ(ios.queue.size(), std::size_t(1));
        FsCopyPath(path + 2 * RTFS_PATH_BYTES, prefix + "/banner.bin");
        std::uintptr_t a[8] = {FsAddr(path + 2 * RTFS_PATH_BYTES), 1, 0x80001088u, 0x80002088u, 0, 0, 0, 0};
        std::uint32_t r2 = 0xDEADu;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), a, &r2), 1);  // the game's open: its transfer in flight
        EXPECT_EQ(r2, 0u);
        EXPECT_EQ(st->fs.busy, 1u);
        const std::uint32_t queued0 = st->queued;
        EXPECT_TRUE(ios.complete_one());  // the stats: the job's delete queues behind the open
        EXPECT_EQ(st->queued, queued0 + 1);
        EXPECT_EQ(st->queue_count, 1u);
        ios.drain();
        bool opened = false;
        for (const FakeIos::Delivery& d : ios.delivered) {
            if (d.cb == 0x80001088u) {
                opened = true;
                EXPECT_TRUE(d.result >= static_cast<std::int32_t>(RTFS_FD_BASE));
                args[0] = static_cast<std::uintptr_t>(d.result);
                EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(2), args, &result), 1);
            }
        }
        EXPECT_TRUE(opened);
        EXPECT_EQ(ios.delivered.back().cb, 0x80001000u);
        EXPECT_EQ(ios.delivered.back().result, RTFAT_EIO);
        EXPECT_EQ(st->job.active, 0u);
        EXPECT_EQ(st->imports, 7u);
        EXPECT_EQ(st->import_jobs, 3u);
        EXPECT_EQ(st->import_failures, 3u);
        EXPECT_FALSE(nand.open);
        EXPECT_TRUE(nand.path == "/tmp/big2.bin");
        EXPECT_FALSE(card_has("big2.bin"));
        EXPECT_EQ(st->fs.busy, 0u);
        EXPECT_EQ(st->queue_count, 0u);
        nand.read_error_after = -1;
    }

    // Without the game's async originals: on a thread the synchronous
    // import at the call, its 0 deferred through a null round trip to
    // the game's callback; from an IPC callback a refusal with -102.
    st->read_async = 0;
    rt_host_fs_in_thread = 1;
    NandFill(nand, "/tmp/sync.bin", 500, 21);
    const std::vector<std::uint8_t> syncbin = nand.bytes;
    rename_args("/tmp/sync.bin", prefix + "/sync.bin");
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(6), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(st->imports, 8u);
    EXPECT_EQ(st->import_jobs, 3u);
    EXPECT_TRUE(nand.path.empty());  // done at the call
    EXPECT_EQ(ios.queue.size(), std::size_t(1));  // the deferred delivery
    ios.drain();
    EXPECT_EQ(ios.delivered.back().result, 0);
    check_card_file("sync.bin", syncbin, 400);
    rt_host_fs_in_thread = 0;
    NandFill(nand, "/tmp/again.bin", 50, 3);
    rename_args("/tmp/again.bin", prefix + "/again.bin");
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(6), args, &result), 1);
    EXPECT_EQ(result, 0u);
    EXPECT_EQ(st->import_refused, 3u);
    EXPECT_EQ(st->imports, 8u);
    ios.drain();
    EXPECT_EQ(ios.delivered.back().result, RTFAT_EACCESS);
    EXPECT_FALSE(card_has("again.bin"));
    EXPECT_TRUE(nand.path == "/tmp/again.bin");
    st->read_async = 0x80100000u;
    rt_host_fs_in_thread = 1;

    // An async request arriving from an IPC callback while a synchronous
    // import holds the engine (injected during one of its card
    // transfers): it queues, and the import's thread-side completions
    // start it, so it runs and its callback is delivered; nothing waits
    // out a timeout.
    {
        static rt_context* s_ctx;
        static std::uint8_t* s_path;
        s_ctx = &ctx;
        s_path = path + 2 * RTFS_PATH_BYTES;
        FsCopyPath(s_path, prefix + "/rksys.dat");
        g_on_transfer = [] {
            std::uintptr_t a[8] = {FsAddr(s_path), 1, 0x80001077u, 0x80002077u, 0, 0, 0, 0};
            std::uint32_t r = 0xDEADu;
            EXPECT_EQ(rt_on_ipc(s_ctx, RT_IPC_ASYNC(1), a, &r), 1);
            EXPECT_EQ(r, 0u);
        };
        NandFill(nand, "/tmp/two.bin", 3000, 17);
        const std::vector<std::uint8_t> two = nand.bytes;
        rename_args("/tmp/two.bin", prefix + "/two.bin");
        const std::uint32_t waits0 = st->waits, timeouts0 = st->wait_timeouts, queued0 = st->queued;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 1);
        EXPECT_EQ(result, 0u);
        EXPECT_EQ(g_on_transfer, nullptr);
        EXPECT_EQ(st->queued, queued0 + 1);
        EXPECT_EQ(st->wait_timeouts, timeouts0);
        EXPECT_TRUE(st->waits >= waits0);
        ios.drain();
        bool opened = false;
        for (const FakeIos::Delivery& d : ios.delivered) {
            if (d.cb == 0x80001077u) {
                opened = true;
                EXPECT_TRUE(d.result >= static_cast<std::int32_t>(RTFS_FD_BASE));
                args[0] = static_cast<std::uintptr_t>(d.result);
                EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(2), args, &result), 1);
            }
        }
        EXPECT_TRUE(opened);
        EXPECT_EQ(ios.delivered.back().cb, 0x80001077u);  // the open's; the sync rename answered at the call
        EXPECT_EQ(st->imports, 9u);
        check_card_file("two.bin", two, 2900);
        EXPECT_EQ(st->fs.busy, 0u);
        EXPECT_EQ(st->queue_count, 0u);
    }

    rt_host_fs_open_async = nullptr;
    rt_host_fs_close_async = nullptr;
    rt_host_fs_read_async = nullptr;
    rt_host_fs_ioctl_async = nullptr;
    rt_host_game_callback = nullptr;
    rt_host_fs_issue = nullptr;
    rt_host_fs_defer = nullptr;
    rt_host_fs_wait = nullptr;
    rt_host_fs_open_sync = nullptr;
    rt_host_fs_close_sync = nullptr;
    rt_host_fs_read_sync = nullptr;
    rt_host_fs_ioctl_sync = nullptr;
    rt_host_fs_transfer = nullptr;
    g_cb_log = nullptr;
    g_fs_card = nullptr;
    g_ios = nullptr;
    g_nand = nullptr;
}


// ---- <savegame clone> (slice 6) ------------------------------------------------

// Kirby's Epic Yarn's save creation as the card sees it: a card of one
// sector per cluster, thirty-two asynchronous renames from /tmp (FLF.bin
// 43456, thirty 131072-byte thumbnails, banner.bin 61600: the directory
// grows into a third cluster), every file's size and bytes right
// afterwards.
static void TestFsJobKirby() {
    FsMemory mem;
    if (!mem.ok) {
        std::cerr << "note: no 32-bit addressable buffer on this host, skipping the FS Kirby drive" << std::endl;
        return;
    }
    FsCard card(1, 9000);
    fatimg::Bytes content;
    rtfat_volume volume;
    FsFillCard(card, content, volume, 5);
    rt_fs_state* st = mem.st;
    std::uint8_t* path = mem.path;
    std::uint8_t* data = mem.data;
    std::uint32_t* out = mem.out;
    const std::string prefix = "/title/00010000/524b3545/data";
    EXPECT_EQ(rtfs_init(&st->fs, &volume, prefix.c_str(), -1), RTFAT_OK);
    st->open_sync = st->close_sync = st->read_sync = st->ioctl_sync = st->ioctlv_sync = 0x80100000u;
    st->open_async = st->close_async = st->read_async = 0x80100000u;
    rt_context ctx{};
    ctx.magic = RT_CONTEXT_MAGIC;
    ctx.flags = RT_FLAG_FS;
    ctx.fs_state = FsAddr(st);
    ctx.di_read_entry = 0x80100000u;
    g_fs_card = &card;
    rt_host_fs_transfer = FsTransfer;
    rt_host_fs_open_sync = NandOpenSync;
    rt_host_fs_close_sync = NandCloseSync;
    rt_host_fs_read_sync = NandReadSync;
    rt_host_fs_ioctl_sync = NandIoctlSync;
    FakeIos ios;
    ios.card = &card;
    ios.ctx = &ctx;
    g_ios = &ios;
    rt_host_fs_issue = IosIssue;
    rt_host_fs_defer = IosDefer;
    rt_host_fs_wait = IosWait;
    rt_host_fs_open_async = IosOpenAsync;
    rt_host_fs_close_async = IosCloseAsync;
    rt_host_fs_read_async = IosReadAsync;
    rt_host_fs_ioctl_async = IosIoctlAsync;
    rt_host_game_callback = GameCb;
    FakeNand nand;
    g_nand = &nand;
    rt_host_fs_in_thread = 0;

    std::uintptr_t args[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::uint32_t result = 0xDEADu;
    std::vector<std::pair<std::string, std::size_t>> files;
    files.push_back({"FLF.bin", 43456});
    for (int i = 0; i < 30; ++i) {
        char n[16];
        std::snprintf(n, sizeof n, "GF_%d_%02d.jpg", i / 10, i % 10);
        files.push_back({n, 131072});
    }
    files.push_back({"banner.bin", 61600});
    std::vector<std::vector<std::uint8_t>> bytes;
    for (std::size_t i = 0; i < files.size(); ++i) {
        // As the game does it: open (-106), GetAttr (-106), the rename.
        FsCopyPath(path, prefix + "/" + files[i].first);
        args[0] = FsAddr(path); args[1] = 1; args[2] = 0x80001000u; args[3] = 0x80002000u;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
        ios.drain();
        EXPECT_EQ(ios.delivered.back().result, RTFAT_ENOENT);
        NandFill(nand, "/tmp/" + files[i].first, files[i].second, static_cast<std::uint8_t>(3 * i + 7));
        bytes.push_back(nand.bytes);
        FsCopyPath(path, "/tmp/" + files[i].first);
        FsCopyPath(path + RTFS_PATH_BYTES, prefix + "/" + files[i].first);
        args[0] = 9; args[1] = RTFS_IOCTL_RENAME; args[2] = FsAddr(path); args[3] = 2 * RTFS_PATH_BYTES; args[4] = 0; args[5] = 0;
        args[6] = 0x80001000u; args[7] = 0x80002000u;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(6), args, &result), 1);
        EXPECT_EQ(result, 0u);
        ios.drain();
        EXPECT_EQ(ios.delivered.back().result, 0);
        EXPECT_TRUE(nand.path.empty());
        // Then the file is opened, its stats asked and it is closed.
        FsCopyPath(path, prefix + "/" + files[i].first);
        args[0] = FsAddr(path); args[1] = 1; args[2] = 0x80001000u; args[3] = 0x80002000u;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 1);
        ios.drain();
        const std::int32_t fd = ios.delivered.back().result;
        EXPECT_TRUE(fd >= static_cast<std::int32_t>(RTFS_FD_BASE));
        args[0] = static_cast<std::uintptr_t>(fd); args[1] = RTFS_IOCTL_GETFILESTATS; args[2] = 0; args[3] = 0; args[4] = FsAddr(out); args[5] = 8;
        args[6] = 0x80001000u; args[7] = 0x80002000u;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(6), args, &result), 1);
        ios.drain();
        EXPECT_EQ(ios.delivered.back().result, 0);
        EXPECT_EQ(out[0], static_cast<std::uint32_t>(files[i].second));
        args[0] = static_cast<std::uintptr_t>(fd); args[1] = 0x80001000u; args[2] = 0x80002000u;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(2), args, &result), 1);
        ios.drain();
    }
    EXPECT_EQ(st->imports, 32u);
    EXPECT_EQ(st->import_jobs, 32u);
    EXPECT_EQ(st->import_failures, 0u);
    ExpectNoStage(card);
    // Every file: its size from a fresh lookup, and its last bytes.
    for (std::size_t i = 0; i < files.size(); ++i) {
        FsCopyPath(path, prefix + "/" + files[i].first);
        args[0] = FsAddr(path); args[1] = 1;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
        const std::uint32_t fd = result;
        args[0] = fd; args[1] = RTFS_IOCTL_GETFILESTATS; args[2] = 0; args[3] = 0; args[4] = FsAddr(out); args[5] = 8;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 1);
        EXPECT_EQ(out[0], static_cast<std::uint32_t>(files[i].second));
        const std::uint32_t at = static_cast<std::uint32_t>(files[i].second) - 40;
        args[0] = fd; args[1] = at; args[2] = 0;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(5), args, &result), 1);
        args[0] = fd; args[1] = FsAddr(data); args[2] = 40;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(3), args, &result), 1);
        EXPECT_EQ(result, 40u);
        EXPECT_TRUE(std::memcmp(data, bytes[i].data() + at, 40) == 0);
        args[0] = fd;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(2), args, &result), 1);
    }

    rt_host_fs_in_thread = 1;
    rt_host_fs_open_async = nullptr;
    rt_host_fs_close_async = nullptr;
    rt_host_fs_read_async = nullptr;
    rt_host_fs_ioctl_async = nullptr;
    rt_host_game_callback = nullptr;
    rt_host_fs_issue = nullptr;
    rt_host_fs_defer = nullptr;
    rt_host_fs_wait = nullptr;
    rt_host_fs_open_sync = nullptr;
    rt_host_fs_close_sync = nullptr;
    rt_host_fs_read_sync = nullptr;
    rt_host_fs_ioctl_sync = nullptr;
    rt_host_fs_transfer = nullptr;
    g_fs_card = nullptr;
    g_ios = nullptr;
    g_nand = nullptr;
}

static void TestFsClone() {
    FsMemory mem;
    if (!mem.ok) {
        std::cerr << "note: no 32-bit addressable buffer on this host, skipping the FS clone drive" << std::endl;
        return;
    }
    FsCard card;
    fatimg::Bytes content;
    rtfat_volume volume;
    FsFillCard(card, content, volume, 11, true);
    rt_fs_state* st = mem.st;
    std::uint8_t* path = mem.path;
    std::uint8_t* data = mem.data;
    std::uint32_t* out = mem.out;
    const std::string prefix = "/title/00010000/524b3545/data";
    EXPECT_EQ(rtfs_init(&st->fs, &volume, prefix.c_str(), -1), RTFAT_OK);
    st->open_sync = st->close_sync = st->read_sync = st->ioctl_sync = st->ioctlv_sync = 0x80100000u;

    rt_context ctx{};
    ctx.magic = RT_CONTEXT_MAGIC;
    ctx.flags = RT_FLAG_FS;
    ctx.fs_state = FsAddr(st);
    g_fs_card = &card;
    rt_host_fs_transfer = FsTransfer;
    rt_host_fs_open_sync = NandOpenSync;
    rt_host_fs_close_sync = NandCloseSync;
    rt_host_fs_read_sync = NandReadSync;
    rt_host_fs_ioctl_sync = NandIoctlSync;
    rt_host_fs_ioctlv_sync = NandIoctlvSync;
    FakeNand nand;
    g_nand = &nand;
    nand.dir_prefix = prefix;
    std::vector<std::uint8_t> flf(2 * RT_FS_IMPORT_BYTES + 77), gf(1061);
    for (std::size_t i = 0; i < flf.size(); ++i) flf[i] = static_cast<std::uint8_t>(i * 7 + 1);
    for (std::size_t i = 0; i < gf.size(); ++i) gf[i] = static_cast<std::uint8_t>(i * 3 + 9);
    nand.dir.push_back({"FLF.bin", flf});
    nand.dir.push_back({"sub", {}});  // a subdirectory: its open fails, it is skipped
    nand.dir.push_back({"GF_0_00.jpg", gf});

    std::uintptr_t args[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::uint32_t result = 0xDEADu;
    const auto check_card_file = [&](const std::string& name, const std::vector<std::uint8_t>& expect, std::uint32_t at) {
        FsCopyPath(path, prefix + "/" + name);
        args[0] = FsAddr(path); args[1] = 1;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 1);
        EXPECT_TRUE(static_cast<std::int32_t>(result) >= static_cast<std::int32_t>(RTFS_FD_BASE));
        const std::uint32_t fd = result;
        args[0] = fd; args[1] = RTFS_IOCTL_GETFILESTATS; args[2] = 0; args[3] = 0; args[4] = FsAddr(out); args[5] = 8;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(6), args, &result), 1);
        EXPECT_EQ(result, 0u);
        EXPECT_EQ(out[0], static_cast<std::uint32_t>(expect.size()));
        args[0] = fd; args[1] = at; args[2] = 0;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(5), args, &result), 1);
        const std::uint32_t n = std::min<std::uint32_t>(64, static_cast<std::uint32_t>(expect.size()) - at);
        std::memset(data, 0xEE, 64);
        args[0] = fd; args[1] = FsAddr(data); args[2] = n;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(3), args, &result), 1);
        EXPECT_EQ(result, n);
        EXPECT_TRUE(std::memcmp(data, expect.data() + at, n) == 0);
        args[0] = fd;
        EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(2), args, &result), 1);
        // Leave args as the replayed open of another device the clone
        // steps below issue, never the closed fd read as a path.
        FsCopyPath(path, "/dev/stm/immediate");
        args[0] = FsAddr(path); args[1] = 0; args[2] = 0x80001000u; args[3] = 0x80002000u;
    };

    // From an IPC callback the clone waits (the synchronous functions
    // cannot run there); the request itself is answered as usual.
    st->clone_pending = 1;
    rt_host_fs_in_thread = 0;
    FsCopyPath(path, "/dev/stm/immediate");
    args[0] = FsAddr(path); args[1] = 0; args[2] = 0x80001000u; args[3] = 0x80002000u;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC(1), args, &result), 0);
    EXPECT_EQ(st->clone_pending, 1u);
    EXPECT_EQ(st->clones, 0u);
    rt_host_fs_in_thread = 1;
    // The marker: the second entry of the folder, hidden.
    const std::size_t marker_at = std::size_t(volume.data_lba + volume.sectors_per_cluster) * 512 + 32;
    EXPECT_EQ(card.image.bytes[marker_at], std::uint8_t('R'));
    EXPECT_EQ(card.image.bytes[marker_at + 11], std::uint8_t(0x22));

    // The game's first call on a thread (a sync open of another device,
    // replayed): the NAND directory is listed and copied in first. The
    // subdirectory entry is skipped and counted; nothing on NAND is
    // deleted; the clone's own /dev/fs fd is closed and not learned.
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 0);
    EXPECT_EQ(st->clone_pending, 0u);
    EXPECT_EQ(st->clones, 1u);
    EXPECT_EQ(st->clone_files, 2u);
    EXPECT_EQ(st->clone_failures, 1u);
    EXPECT_EQ(nand.readdirs, 2u);
    EXPECT_EQ(nand.opens, 2u);
    EXPECT_EQ(nand.closes, 2u);
    EXPECT_EQ(nand.fs_opens, 1u);
    EXPECT_EQ(nand.fs_closes, 1u);
    EXPECT_TRUE(nand.deleted.empty());
    EXPECT_EQ(st->fs.fs_fd, -1);
    EXPECT_EQ(st->fs.busy, 0u);
    // The clone ran to its end: the marker is deleted (0xE5).
    EXPECT_EQ(st->clone_marker, 1u);
    EXPECT_EQ(card.image.bytes[marker_at], std::uint8_t(0xE5));
    check_card_file("FLF.bin", flf, 0);
    check_card_file("FLF.bin", flf, 2 * RT_FS_IMPORT_BYTES + 77 - 40);
    check_card_file("GF_0_00.jpg", gf, 1000);
    ExpectNoStage(card);
    EXPECT_EQ(st->imports, 0u);

    // Once only.
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 0);
    EXPECT_EQ(st->clones, 1u);
    EXPECT_EQ(nand.readdirs, 2u);

    // A NAND directory that does not exist: nothing to copy, not a failure.
    st->clone_pending = 1;
    nand.dir_prefix = "/title/00010000/00000000/data";
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 0);
    EXPECT_EQ(st->clones, 2u);
    EXPECT_EQ(st->clone_failures, 1u);
    EXPECT_EQ(st->clone_files, 2u);
    EXPECT_EQ(st->clone_marker, 1u);

    // More names than the listing holds (RT_FS_CLONE_MAX): the first
    // RT_FS_CLONE_MAX are attempted (two files copied, the subdirectories
    // fail), every name past the cap is a failure too.
    st->clone_pending = 1;
    nand.dir_prefix = prefix;
    nand.dir.clear();
    nand.dir.push_back({"A.bin", {1, 2, 3}});
    nand.dir.push_back({"B.bin", {4, 5, 6, 7}});
    for (std::uint32_t i = 0; i < RT_FS_CLONE_MAX; ++i) nand.dir.push_back({"sub", {}});
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 0);
    EXPECT_EQ(st->clones, 3u);
    EXPECT_EQ(st->clone_files, 4u);
    EXPECT_EQ(st->clone_failures, 1u + RT_FS_CLONE_MAX);
    check_card_file("B.bin", {4, 5, 6, 7}, 0);

    // Without the game's synchronous ioctlv the listing is impossible:
    // the clone is given up, counted, and never retried.
    st->clone_pending = 1;
    st->ioctlv_sync = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_SYNC(1), args, &result), 0);
    EXPECT_EQ(st->clone_pending, 0u);
    EXPECT_EQ(st->clones, 3u);
    EXPECT_EQ(st->clone_failures, 2u + RT_FS_CLONE_MAX);

    rt_host_fs_open_sync = nullptr;
    rt_host_fs_close_sync = nullptr;
    rt_host_fs_read_sync = nullptr;
    rt_host_fs_ioctl_sync = nullptr;
    rt_host_fs_ioctlv_sync = nullptr;
    rt_host_fs_transfer = nullptr;
    g_fs_card = nullptr;
    g_nand = nullptr;
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
    EXPECT_TRUE(sizeof(rt_context) <= riftwii::kResidentContextBytes);  // the slot rt_entry.S reserves
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
    EXPECT_EQ(in_count, 2u);
    EXPECT_EQ(out_count, 1u);
    EXPECT_EQ(callback, 0x935D0100u);
    if (ioctl == RT_SDHC_READ) {
        // d2x's /dev/sdio/sdhc: sector and count as 4-byte inputs, the data out.
        EXPECT_EQ(vec[0].len, 4u);
        EXPECT_EQ(vec[1].len, 4u);
        const std::uint32_t sector = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(vec[0].data));
        const std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(vec[1].data));
        EXPECT_EQ(vec[2].data, record->bounce);
        EXPECT_EQ(vec[2].len, count * 512u);
        g_sd_sectors_requested.push_back(sector);
        g_sd_sectors_requested.push_back(count);
        if (sector + count > 128) return -4;
        std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(record->bounce)), g_card + sector * 512,
                    count * 512);
        return 0;
    }
    EXPECT_EQ(ioctl, 7u);
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

    // The game on the SD card: the same read through d2x's /dev/sdio/sdhc
    // (sector numbers, READ ioctlv) gives the same bytes.
    ctx.sdio_sdhc = RT_SD_D2X;
    std::memset(out, 0xEE, 0x800);
    args[6] = 0x80005000;
    args[7] = 0x80006000;
    g_sd_sectors_requested.clear();
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    rec = reinterpret_cast<rt_pending*>(args[7]);
    di_result = 1;
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    replies = 0;
    while (cb == 0 && replies < 10) {
        sd_result = 0;
        rt_on_di_complete(&ctx, &sd_result, rec, &cb, &ud);
        ++replies;
    }
    EXPECT_EQ(replies, 2);
    EXPECT_EQ(g_sd_sectors_requested.size(), 4u);
    if (g_sd_sectors_requested.size() == 4u) {
        EXPECT_EQ(g_sd_sectors_requested[0], 10u);
        EXPECT_EQ(g_sd_sectors_requested[1], 2u);
        EXPECT_EQ(g_sd_sectors_requested[2], 40u);
        EXPECT_EQ(g_sd_sectors_requested[3], 3u);
    }
    EXPECT_EQ(sd_result, 1);
    EXPECT_EQ(std::memcmp(out + 16, expected.data(), 2000), 0);
    EXPECT_EQ(out[16 + 2000], 0x5A);
    ctx.sdio_sdhc = 1;
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

    // DISC sources retain their full 64-bit partition byte offset until the
    // /dev/di command is encoded in words. This address is above 4 GiB but
    // below the 16 GiB (32-bit word) command limit.
    constexpr std::uint64_t kHighDiscSource = 0x100007005ull;
    constexpr std::uint32_t kHighDiscWord = 0x40001C00u;
    dr.disc_offset = kHighDiscSource;
    EXPECT_TRUE(riftwii::build_payload(Pieces({}, {}, {dr}), table_address, 0, 9, payload, error));
    std::memcpy(low_table, payload.data(), payload.size());
    g_disc_requests.clear();
    std::memset(out, 0xEE, 0x800);
    di_cmd[1] = 0x80;
    di_cmd[2] = 0x60000 >> 2;
    args[4] = reinterpret_cast<std::uintptr_t>(out);
    args[5] = 0x80;
    args[6] = 0x80005000;
    args[7] = 0x80006000;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    rec = reinterpret_cast<rt_pending*>(args[7]);
    di_result = 1;
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0u);
    EXPECT_EQ(rec->phase, RT_PHASE_DISC_RUN);
    EXPECT_EQ(g_disc_requests.size(), 2u);
    EXPECT_EQ(g_disc_requests[0], kHighDiscWord);
    EXPECT_EQ(g_disc_requests[1], 0x80u);
    EXPECT_EQ(rec->chunk_skip, 5u);
    EXPECT_EQ(rec->chunk_bytes, 100u);
    di_result = 1;
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0x80005000u);
    EXPECT_EQ(di_result, 1);
    for (std::uint32_t k = 0; k < 100; ++k) {
        if (out[k] != static_cast<std::uint8_t>(((kHighDiscWord << 2) + 5u + k) * 3u)) {
            EXPECT_TRUE(false);
            break;
        }
    }

    // An entry that would cross the 16 GiB byte boundary cannot be encoded
    // in the DI word field. It completes with the normal DI error rather
    // than issuing a wrapped read.
    dr.disc_offset = (std::uint64_t(1) << 34) - 16;
    dr.length = 32;
    EXPECT_TRUE(riftwii::build_payload(Pieces({}, {}, {dr}), table_address, 0, 9, payload, error));
    std::memcpy(low_table, payload.data(), payload.size());
    g_disc_requests.clear();
    args[6] = 0x80005000;
    args[7] = 0x80006000;
    const std::uint32_t failures_before_range = ctx.disc_failures;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
    rec = reinterpret_cast<rt_pending*>(args[7]);
    di_result = 1;
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(cb, 0x80005000u);
    EXPECT_EQ(di_result, RT_DI_ERROR);
    EXPECT_EQ(g_disc_requests.size(), std::size_t(0));
    EXPECT_EQ(ctx.disc_failures, failures_before_range + 1u);
    EXPECT_EQ(rec->in_use, 0u);

    // The drive's error is passed to the game as it is.
    dr.disc_offset = 0x7005;
    dr.length = 100;
    EXPECT_TRUE(riftwii::build_payload(Pieces({}, {}, {dr}), table_address, 0, 9, payload, error));
    std::memcpy(low_table, payload.data(), payload.size());
    const std::uint32_t failures_before_drive = ctx.disc_failures;
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
    EXPECT_EQ(ctx.disc_failures, failures_before_drive + 1u);
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

    // A replacement already in memory (the FST) points there and takes no
    // room in the payload.
    riftwii::MemReplacement fst = a;
    fst.in_place = 0x817CC740;
    EXPECT_TRUE(riftwii::build_mem_payload({fst, b}, 0x935D2000, 7, payload, error));
    header = reinterpret_cast<const rt_header*>(payload.data());
    EXPECT_EQ(rt_validate(header, payload.size()), RT_OK);
    EXPECT_EQ(rt_entries(header)[0].source, 0x935D2000ull + table_bytes);
    EXPECT_EQ(rt_entries(header)[1].source, 0x817CC740ull);
    EXPECT_EQ(rt_entries(header)[1].length, 3610ull);
    EXPECT_EQ(payload.size(), table_bytes + 32);

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

    // DVDLowSeek has no data callback to redirect, but games can seek to a
    // resized/created virtual file before they read it.  Keep that async IOS
    // request intact while directing the impossible virtual address at word
    // zero.  A physical dual-layer address below the virtual window remains
    // a real drive seek.
    std::uint32_t seek_cmd[8] = {0xAB000000, 0, 0x80000010u, 0, 0, 0, 0, 0};
    std::uintptr_t seek_args[8] = {3, 0xAB, reinterpret_cast<std::uintptr_t>(seek_cmd), 0x20,
                                   0, 0, 0x80005000, 0x80006000};
    // Through the dispatcher the trampoline calls: a seek must reach the
    // DI hook, not the savegame path.
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC_IOCTL, seek_args, &result), 0);
    EXPECT_EQ(seek_cmd[2], 0u);
    seek_cmd[2] = 0x80000010u;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, seek_args, &result), 0);
    EXPECT_EQ(seek_cmd[2], 0u);
    EXPECT_EQ(seek_args[6], 0x80005000u);  // the hook did not call or replace it
    EXPECT_EQ(seek_args[7], 0x80006000u);

    seek_cmd[2] = 0x7ED37FFFu;  // nominal physical dual-layer range, below virtual_start_words
    EXPECT_EQ(rt_on_ioctl_async(&ctx, seek_args, &result), 0);
    EXPECT_EQ(seek_cmd[2], 0x7ED37FFFu);

    seek_cmd[0] = 0xAC000000;  // malformed command block: leave it alone
    seek_cmd[2] = 0x80000010u;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, seek_args, &result), 0);
    EXPECT_EQ(seek_cmd[2], 0x80000010u);
    seek_cmd[0] = 0xAB000000;
    seek_args[3] = 0x1C;       // malformed command size: leave it alone
    EXPECT_EQ(rt_on_ioctl_async(&ctx, seek_args, &result), 0);
    EXPECT_EQ(seek_cmd[2], 0x80000010u);
    seek_args[1] = 0xAC;       // another DI request carrying seek-shaped input is not a seek
    seek_args[3] = 0x20;
    EXPECT_EQ(rt_on_ioctl_async(&ctx, seek_args, &result), 0);
    EXPECT_EQ(seek_cmd[2], 0x80000010u);

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

// A read over more pieces than one record holds (RT_MAX_RUNS) is served
// window by window, never passed through with the mod half missing.
static void TestWindowedRead() {
    std::uint8_t* low = LowBuffer(0x10000);
    if (!low) {
        std::cerr << "note: no 32-bit addressable buffer on this host, skipping the windowed read" << std::endl;
        return;
    }
    // 12 replacements of 4 bytes, 8 bytes apart from 0x1000: 23 runs.
    std::vector<riftwii::MemReplacement> reps;
    for (std::uint8_t i = 0; i < 12; ++i) {
        riftwii::MemReplacement m;
        m.virtual_offset = 0x1000 + i * 8u;
        m.bytes.assign(4, static_cast<std::uint8_t>(i + 1));
        reps.push_back(m);
    }
    std::uint8_t* out = low;
    std::uint8_t* table_copy = low + 0x1000;
    const std::uint32_t table_address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(table_copy));
    std::vector<std::uint8_t> payload;
    std::string error;
    EXPECT_TRUE(riftwii::build_mem_payload(reps, table_address, 0, payload, error));
    std::memcpy(table_copy, payload.data(), payload.size());

    rt_context ctx{};
    ctx.magic = RT_CONTEXT_MAGIC;
    ctx.table = table_address;
    ctx.complete_entry = 0x935D0100;
    std::memset(out, 0xEE, 0x80);
    std::uint32_t di_cmd[8] = {0x71000000, 0x60, 0x1000 >> 2, 0, 0, 0, 0, 0};
    std::uintptr_t args[8] = {3, 0x71, reinterpret_cast<std::uintptr_t>(di_cmd), 0x20,
                              reinterpret_cast<std::uintptr_t>(out), 0x60, 0x80005000, 0x80006000};
    std::uint32_t result = 0;
    EXPECT_EQ(rt_on_ipc(&ctx, RT_IPC_ASYNC_IOCTL, args, &result), 0);
    EXPECT_EQ(args[6], 0x935D0100u);  // redirected, not passed through
    EXPECT_EQ(ctx.run_overflow, 1u);
    auto* rec = reinterpret_cast<rt_pending*>(args[7]);
    EXPECT_EQ(rec->run_count, RT_MAX_RUNS);
    EXPECT_TRUE(rec->covered < rec->length);

    std::uintptr_t cb = 0, ud = 0;
    std::int32_t di_result = 1;
    rt_on_di_complete(&ctx, &di_result, rec, &cb, &ud);
    EXPECT_EQ(di_result, 1);
    EXPECT_EQ(cb, 0x80005000u);
    EXPECT_EQ(ud, 0x80006000u);
    EXPECT_EQ(rec->in_use, 0u);
    for (std::uint32_t i = 0; i < 12; ++i) {
        EXPECT_EQ(out[i * 8], i + 1);
        EXPECT_EQ(out[i * 8 + 3], i + 1);
        EXPECT_EQ(out[i * 8 + 4], 0xEE);  // the gaps keep the disc's bytes
    }
    EXPECT_EQ(out[0x5F], 0xEE);
    EXPECT_EQ(out[0x60], 0xEE);  // nothing written past the request
}

int main() {
    TestBlob();
    TestJumpAndDisplace();
    TestPlacement();
    TestSymbolSearch();
    TestIpcApiSearch();
    TestFsIpcTranslation();
    TestFsSyncIntercept();
    TestFsAsyncIntercept();
    TestFsRenameImport();
    TestFsJobKirby();
    TestFsClone();
    TestResidentHandler();
    TestPayloadAndRedirect();
    TestVirtualWindow();
    TestWindowedRead();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "hook tests passed" << std::endl;
    return 0;
}
