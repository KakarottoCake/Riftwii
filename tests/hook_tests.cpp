// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/hook.hpp"
#include "riftwii/symsearch.hpp"
#include "rt_hook.h"
#include "rtable.h"

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
    Bytes b(0x200, 0);
    Put32(b, 0, RT_BLOB_MAGIC);
    Put32(b, 4, RT_BLOB_VERSION);
    Put32(b, 8, 0x200);
    Put32(b, 12, 0x100);  // context
    Put32(b, 16, 0x20);   // hook
    Put32(b, 20, 0xA0);   // replay
    Put32(b, 24, 0xB0);   // continue
    Put32(b, 28, 0xC0);   // completion entry
    Put32(b, 0x100, RT_CONTEXT_MAGIC);
    return b;
}

static void TestBlob() {
    riftwii::ResidentBlob rb;
    std::string error;
    Bytes b = MakeBlob();
    EXPECT_TRUE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    EXPECT_EQ(rb.size, 0x200u);
    EXPECT_EQ(rb.context_offset, 0x100u);
    EXPECT_EQ(rb.hook_ioctl_async_offset, 0x20u);
    EXPECT_EQ(rb.replay_ioctl_async_offset, 0xA0u);
    EXPECT_EQ(rb.continue_ioctl_async_offset, 0xB0u);
    EXPECT_EQ(rb.complete_di_offset, 0xC0u);

    b = MakeBlob();
    Put32(b, 0, 0x12345678);
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 4, 99);
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 8, 0x1E0);  // size field disagrees with the data
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 12, 0x1F0);  // context would run past the end
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 24, 0xB4);  // continue slot must follow the replay slot
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 28, 0x1FE);  // completion entry past the end
    EXPECT_FALSE(riftwii::parse_resident_blob(b.data(), b.size(), rb, error));
    b = MakeBlob();
    Put32(b, 0x100, 0);  // context magic
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
}

// ---- placement ------------------------------------------------------------

static void TestPlacement() {
    riftwii::ResidentPlacement p;
    std::string error;
    EXPECT_TRUE(riftwii::plan_resident_placement(0x935E0000, 1120, 0, p, error));
    EXPECT_EQ(p.base, 0x935D0000u);
    EXPECT_EQ(p.reserved_bytes, 0x10000u);
    EXPECT_EQ(p.new_arena_end, 0x935D0000u);
    EXPECT_TRUE(riftwii::plan_resident_placement(0x935E0000, 1120, 0x10000, p, error));
    EXPECT_EQ(p.base, 0x935C0000u);
    EXPECT_TRUE(riftwii::plan_resident_placement(0x933E0000, 0x20000, 0, p, error));  // IOS58-style end
    EXPECT_EQ(p.base, 0x933C0000u);
    EXPECT_TRUE(riftwii::plan_resident_placement(0x935DFFE0, 32, 0, p, error));  // unaligned end rounds down
    EXPECT_EQ(p.base, 0x935C0000u);
    EXPECT_FALSE(riftwii::plan_resident_placement(0x80000000, 32, 0, p, error));  // MEM1
    EXPECT_FALSE(riftwii::plan_resident_placement(0x90800000, 32, 0, p, error));  // at the floor
    EXPECT_FALSE(riftwii::plan_resident_placement(0x935E0000, 0x03000000, 0, p, error));  // too big
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
    riftwii::CodeRange range() const { return {base, bytes.data(), bytes.size()}; }
};

}  // namespace

static void TestSymbolSearch() {
    FakeText t;
    // Functions first: IOS_IoctlAsync at +0, IOS_IoctlvAsync at +0x10, a decoy at +0x20.
    const std::uint32_t ioctl_async = t.here();
    t.prologue();
    const std::uint32_t ioctlv_async = t.here();
    t.prologue();
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

    // Two text ranges: sites in one, function in the other.
    FakeText fn;
    fn.base = 0x80100000;
    const std::uint32_t h = fn.here();
    fn.prologue();
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

    EXPECT_EQ(rt_on_ioctl_async(&ctx, args, &result), 0);
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
    std::uint8_t* low = LowBuffer(0x10000);
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
    rt_on_di_complete(&ctx, 1, rec, &cb, &ud);
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
    rt_on_di_complete(&ctx, -4, rec, &cb, &ud);
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
}

int main() {
    TestBlob();
    TestJumpAndDisplace();
    TestPlacement();
    TestSymbolSearch();
    TestResidentHandler();
    TestPayloadAndRedirect();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "hook tests passed" << std::endl;
    return 0;
}
