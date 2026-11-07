// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wiimenucontent.hpp"
#include "log.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "riftwii/sysfont.hpp"

using namespace riftwii;
using namespace riftwii::wii;

namespace {
int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << '\n'; ++failures; } } while (0)

constexpr u64 kSystemMenu = 0x0000000100000002ull;
std::vector<u8> tmd_bytes;
std::string fail_call;
std::string logs;
u32 reported_size = 0;
u32 ticket_count = 2;
int open_calls = 0;
int close_calls = 0;
int read_calls = 0;
s32 file_size = 64;
s32 read_size = 64;
bool refuse_first_view = false;
int nand_calls = 0;
bool nand_works = false;
int permission_opens = 0;
int permission_closes = 0;

bool Aligned(const void* pointer) {
    return reinterpret_cast<std::uintptr_t>(pointer) % 32 == 0;
}

void Put16(std::size_t at, u16 value) {
    tmd_bytes[at] = value >> 8;
    tmd_bytes[at + 1] = value;
}

void Reset(unsigned contents = 2) {
    fail_call.clear();
    logs.clear();
    ticket_count = 2;
    open_calls = 0;
    close_calls = 0;
    read_calls = 0;
    file_size = 64;
    read_size = 64;
    refuse_first_view = false;
    nand_calls = 0;
    nand_works = false;
    permission_opens = 0;
    permission_closes = 0;
    tmd_bytes.assign(0x1e4 + contents * 36, 0);
    tmd_bytes[0x18f] = 1;
    tmd_bytes[0x193] = 2;
    Put16(0x1de, contents);
    Put16(0x1e0, 7);
    for (unsigned i = 0; i < contents; ++i) {
        const std::size_t record = 0x1e4 + i * 36;
        tmd_bytes[record + 3] = 0xab;  // Content ID differs from its index.
        Put16(record + 4, i == 0 ? 7 : 23);
        Put16(record + 6, i == 0 ? 1 : 0x8001);
        tmd_bytes[record + 15] = 64;
        if (i == 1) std::memcpy(tmd_bytes.data() + record + 16, kWiiFontHash, 20);
    }
    reported_size = static_cast<u32>(tmd_bytes.size());
}

bool ReadContent(const WiiMenuContentReader& reader, const TmdContent& content,
                 MenuContentBuffer& bytes, std::size_t& size, std::string& why) {
    return reader.Read(content, [&bytes](std::size_t length, std::string& error) {
        bytes = AllocateMenuContentBuffer(length, error);
        return bytes.get();
    }, size, why);
}

void TestSharedIndexAndAlignment() {
    Reset();
    WiiMenuContentReader reader("Menu font");
    std::string why;
    EXPECT(reader.Open(why));
    EXPECT(reader.Metadata().boot_index == 7);
    const TmdContent* content = reader.FindHash(kWiiFontHash);
    EXPECT(content != nullptr);
    EXPECT(reader.FindHash(kWiiKoreanFontHash) == nullptr);
    if (!content) return;
    EXPECT(content->type == 0x8001);
    EXPECT(content->index == 23);
    MenuContentBuffer bytes(nullptr, &std::free);
    std::size_t size = 0;
    refuse_first_view = true;
    EXPECT(ReadContent(reader, *content, bytes, size, why));
    EXPECT(size == 64);
    EXPECT(open_calls == 2);
    EXPECT(read_calls == 1);
    EXPECT(close_calls == 1);
    EXPECT(bytes.get()[63] == 0x5a);
    EXPECT(logs.find("ES_OpenTitleContent failed (-1026)") != std::string::npos);
    EXPECT(logs.find("title metadata gives") == std::string::npos);
}

void TestMetadataFailures() {
    for (const char* call : {"ES_GetStoredTMDSize", "ES_GetStoredTMD", "ES_GetNumTicketViews", "ES_GetTicketViews"}) {
        Reset();
        fail_call = call;
        WiiMenuContentReader reader("Wii Menu sounds");
        std::string why;
        EXPECT(!reader.Open(why));
        EXPECT(why == std::string(call) + " failed (-1026)");
        EXPECT(open_calls == 0);
    }
    for (u32 size : {0u, 65537u, 32u}) {
        Reset();
        reported_size = size;
        WiiMenuContentReader reader("Wii Menu sounds");
        std::string why;
        EXPECT(!reader.Open(why));
    }
    for (u32 count : {0u, 17u}) {
        Reset();
        ticket_count = count;
        WiiMenuContentReader reader("Wii Menu sounds");
        std::string why;
        EXPECT(!reader.Open(why));
    }
    Reset(65);
    WiiMenuContentReader too_many("Wii Menu sounds");
    std::string why;
    EXPECT(!too_many.Open(why));
    Reset();
    tmd_bytes[0x193] = 3;
    WiiMenuContentReader wrong_title("Wii Menu sounds");
    EXPECT(!wrong_title.Open(why));
}

void TestReadFailuresAndBounds() {
    for (const char* call : {"ES_OpenTitleContent", "ES_SeekContent", "ES_ReadContent", "ES_CloseContent"}) {
        Reset();
        WiiMenuContentReader reader("Menu font");
        std::string why;
        EXPECT(reader.Open(why));
        fail_call = call;
        MenuContentBuffer bytes(nullptr, &std::free);
        std::size_t size = 0;
        EXPECT(!ReadContent(reader, reader.Metadata().contents[1], bytes, size, why));
        EXPECT(why == std::string(call) + " failed (-1026)");
        EXPECT(close_calls == (fail_call == "ES_OpenTitleContent" ? 0 : 1));
        // Only a refused open tries the NAND; it failed too, so ES's words stay.
        EXPECT(nand_calls == (fail_call == "ES_OpenTitleContent" ? 1 : 0));
    }
    for (s32 length : {0, 16 * 1024 * 1024 + 1}) {
        Reset();
        WiiMenuContentReader reader("Menu font");
        std::string why;
        EXPECT(reader.Open(why));
        file_size = length;
        MenuContentBuffer bytes(nullptr, &std::free);
        std::size_t size = 0;
        EXPECT(!ReadContent(reader, reader.Metadata().contents[1], bytes, size, why));
        EXPECT(bytes == nullptr);
        EXPECT(size == 0);
        EXPECT(read_calls == 0);
        EXPECT(close_calls == 1);
    }
    Reset();
    WiiMenuContentReader reader("Menu font");
    std::string why;
    EXPECT(reader.Open(why));
    MenuContentBuffer bytes(nullptr, &std::free);
    std::size_t size = 0;
    read_size = 32;
    EXPECT(!ReadContent(reader, reader.Metadata().contents[1], bytes, size, why));
    EXPECT(close_calls == 1);
    EXPECT(size == 64);
    EXPECT(!AllocateMenuContentBuffer(kMaxMenuContentSize + 1, why));
    EXPECT(!AllocateMenuContentBuffer(0, why));

    Reset();
    WiiMenuContentReader unavailable("Menu font");
    EXPECT(!ReadContent(unavailable, TmdContent{}, bytes, size, why));
    EXPECT(open_calls == 0);
    EXPECT(unavailable.Open(why));
    EXPECT(!unavailable.Read(unavailable.Metadata().contents[1],
        [](std::size_t, std::string& error) -> u8* {
            error = "no memory for the test content";
            return nullptr;
        }, size, why));
    EXPECT(why == "no memory for the test content");
    EXPECT(read_calls == 0);
    EXPECT(close_calls == 1);
}

void TestRealContentSize() {
    // Themes can grow or shrink a content without updating its metadata.
    // Even the upper bound is allowed, and every read allocates exactly once.
    for (s32 length : {1, 32, 96, 16 * 1024 * 1024}) {
        Reset();
        WiiMenuContentReader reader("Wii Menu sounds");
        std::string why;
        EXPECT(reader.Open(why));
        file_size = read_size = length;
        MenuContentBuffer bytes(nullptr, &std::free);
        std::size_t size = 0;
        int allocations = 0;
        const auto allocate = [&](std::size_t measured, std::string& error) {
            ++allocations;
            EXPECT(measured == static_cast<std::size_t>(length));
            bytes = AllocateMenuContentBuffer(measured, error);
            return bytes.get();
        };
        const TmdContent& content = reader.Metadata().contents[1];
        EXPECT(reader.Read(content, allocate, size, why));
        EXPECT(allocations == 1);
        EXPECT(size == static_cast<std::size_t>(length));
        EXPECT(bytes.get()[size - 1] == 0x5a);
        const std::string message = "Wii Menu sounds: content 000000ab is " + std::to_string(length) +
            " bytes, not the 64 its title metadata gives (a Wii Menu theme?); it is read at its own size\n";
        EXPECT(logs == message);
        EXPECT(reader.Read(content, allocate, size, why));
        EXPECT(logs == message);  // One size notice per content, even on a retry.
    }
}

// ES refuses every view (a Wii and a vWii started from the RiftWii
// channel, 2610-810): the NAND's copy is read instead.
void TestNandWhenEsRefuses() {
    Reset();
    {
        WiiMenuContentReader reader("Menu font");
        std::string why;
        EXPECT(reader.Open(why));
        fail_call = "ES_OpenTitleContent";
        nand_works = true;
        MenuContentBuffer bytes(nullptr, &std::free);
        std::size_t size = 0;
        file_size = 96;
        EXPECT(ReadContent(reader, reader.Metadata().contents[1], bytes, size, why));
        EXPECT(open_calls == 2);  // each ticket view first
        EXPECT(nand_calls == 1);
        EXPECT(read_calls == 0);
        EXPECT(size == 96);
        EXPECT(bytes.get()[95] == 0xa5);
        EXPECT(logs.find("Menu font: read from the NAND, as ES refused it") != std::string::npos);
        EXPECT(logs.find("content 000000ab is 96 bytes, not the 64 its title metadata gives") != std::string::npos);
        const std::size_t notice = logs.find("(a Wii Menu theme?)");
        EXPECT(ReadContent(reader, reader.Metadata().contents[1], bytes, size, why));
        EXPECT(logs.find("(a Wii Menu theme?)", notice + 1) == std::string::npos);
        EXPECT(permission_opens == 1 && permission_closes == 0);
    }
    EXPECT(permission_closes == 1);

    Reset();
    WiiMenuContentReader failing("Wii Menu sounds");
    std::string why;
    MenuContentBuffer bytes(nullptr, &std::free);
    std::size_t size = 0;
    EXPECT(failing.Open(why));
    fail_call = "ES_OpenTitleContent";
    EXPECT(!ReadContent(failing, failing.Metadata().contents[1], bytes, size, why));
    EXPECT(why == "ES_OpenTitleContent failed (-1026)");
    EXPECT(logs.find("Wii Menu sounds: the NAND's copy could not be read either (no NAND here)") != std::string::npos);
}
}  // namespace

bool riftwii::wii::ReadMenuContentFromNand(const TmdContent& content, const MenuContentAllocator& allocate,
                                         std::size_t& size, const char*, std::string& why,
                                         NandPermissionSession* permissions) {
    EXPECT(permissions != nullptr);
    permissions->Open();
    ++nand_calls;
    EXPECT(content.index == 23);
    if (!nand_works) {
        why = "no NAND here";
        return false;
    }
    size = static_cast<std::size_t>(file_size);
    u8* buffer = allocate(size, why);
    if (!buffer) return false;
    EXPECT(Aligned(buffer));
    std::memset(buffer, 0xa5, size);
    return true;
}

bool riftwii::wii::open_nand_permissions(const char*) {
    ++permission_opens;
    return true;
}
void riftwii::wii::close_nand_permissions(const char*) { ++permission_closes; }

s32 ES_GetStoredTMDSize(u64 title, u32* size) {
    EXPECT(title == kSystemMenu);
    *size = reported_size;
    return fail_call == "ES_GetStoredTMDSize" ? -1026 : 0;
}
s32 ES_GetStoredTMD(u64 title, signed_blob* bytes, u32 size) {
    EXPECT(title == kSystemMenu);
    EXPECT(Aligned(bytes));
    EXPECT(size == reported_size);
    std::memcpy(bytes, tmd_bytes.data(), size);
    return fail_call == "ES_GetStoredTMD" ? -1026 : 0;
}
s32 ES_GetNumTicketViews(u64 title, u32* count) {
    EXPECT(title == kSystemMenu);
    *count = ticket_count;
    return fail_call == "ES_GetNumTicketViews" ? -1026 : 0;
}
s32 ES_GetTicketViews(u64 title, tikview* views, u32 count) {
    EXPECT(title == kSystemMenu);
    EXPECT(Aligned(views));
    EXPECT(count == ticket_count);
    return fail_call == "ES_GetTicketViews" ? -1026 : 0;
}
s32 ES_OpenTitleContent(u64 title, const tikview* view, u16 index) {
    EXPECT(title == kSystemMenu);
    EXPECT(Aligned(view));
    EXPECT(index == 23);
    ++open_calls;
    if (fail_call == "ES_OpenTitleContent" || (refuse_first_view && open_calls == 1)) return -1026;
    return 42;
}
s32 ES_SeekContent(s32 fd, s32 offset, s32 whence) {
    EXPECT(fd == 42);
    EXPECT(offset == 0);
    if (fail_call == "ES_SeekContent") return -1026;
    return whence == SEEK_END ? file_size : 0;
}
s32 ES_ReadContent(s32 fd, void* bytes, u32 size) {
    EXPECT(fd == 42);
    EXPECT(Aligned(bytes));
    EXPECT(size == static_cast<u32>(file_size));
    ++read_calls;
    std::memset(bytes, 0x5a, size);
    return fail_call == "ES_ReadContent" ? -1026 : read_size;
}
s32 ES_CloseContent(s32 fd) {
    EXPECT(fd == 42);
    ++close_calls;
    return fail_call == "ES_CloseContent" ? -1026 : 0;
}
void riftwii::wii::logf(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    logs += line;
}

int main() {
    TestSharedIndexAndAlignment();
    EXPECT(permission_opens == 0);  // ES success never opens the check
    TestMetadataFailures();
    TestReadFailuresAndBounds();
    TestRealContentSize();
    TestNandWhenEsRefuses();
    return failures == 0 ? 0 : 1;
}
