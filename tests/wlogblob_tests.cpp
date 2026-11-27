// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Run the real hook C code in all three layouts against identical traffic.
// The full image with translation disabled is the trace-only reference;
// the unchanged rtpro implementation is the packet/state reference.
extern "C" {
#include "wlog/wlog_hook.h"
#define DECLARE_ENTRIES(mode) \
    void wlog_##mode##_on_print(rt_wlog_context*, const uint32_t*); \
    void wlog_##mode##_on_exit(rt_wlog_context*); \
    uint32_t wlog_##mode##_on_ioctlv(rt_wlog_context*, uint32_t*); \
    int32_t wlog_##mode##_after_ioctlv(rt_wlog_context*, uint32_t, int32_t); \
    int32_t wlog_##mode##_on_bt(rt_wlog_context*, int32_t, uint32_t);
DECLARE_ENTRIES(full)
DECLARE_ENTRIES(trace)
DECLARE_ENTRIES(translate)
#undef DECLARE_ENTRIES
}

#include "wpadhook.hpp"
#include "wlog_images.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
static std::uint8_t* FixedBuffer(std::uintptr_t at, std::size_t bytes) {
    return static_cast<std::uint8_t*>(
        VirtualAlloc(reinterpret_cast<void*>(at), bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
}
#elif defined(__linux__)
#include <sys/mman.h>
static std::uint8_t* FixedBuffer(std::uintptr_t at, std::size_t bytes) {
    void* p = mmap(reinterpret_cast<void*>(at), bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return p == MAP_FAILED || p != reinterpret_cast<void*>(at) ? nullptr : static_cast<std::uint8_t*>(p);
}
#else
static std::uint8_t* FixedBuffer(std::uintptr_t, std::size_t) { return nullptr; }
#endif

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "line " << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

namespace {

constexpr std::uint32_t kInput = 0x80100000;
constexpr std::uint32_t kVectors = kInput + 0x100;
constexpr std::uint32_t kEndpoint = kInput + 0x200;
constexpr std::uint32_t kLength = kInput + 0x220;
constexpr std::uint32_t kFormat = kInput + 0x300;
constexpr std::uint32_t kString = kInput + 0x400;
std::vector<std::uint8_t> saved;
unsigned callback_calls = 0;
std::int32_t callback_result = 0;
unsigned ios_calls = 0;
unsigned fail_call = 0;

std::int32_t IosResult(std::int32_t result) {
    ++ios_calls;
    return ios_calls == fail_call ? -77 : result;
}

template <typename T> T* At(std::uint32_t address) {
    return reinterpret_cast<T*>(static_cast<std::uintptr_t>(address));
}

struct Hook {
    rt_wlog_context context{};
    std::uint32_t (*ioctlv)(rt_wlog_context*, std::uint32_t*);
    std::int32_t (*after)(rt_wlog_context*, std::uint32_t, std::int32_t);
    std::int32_t (*complete)(rt_wlog_context*, std::int32_t, std::uint32_t);
    std::uint32_t bytes;
    rt_wlog_watch* watches;

    Hook(unsigned mode, bool translate) {
        context.state = 0x90000000 + mode * 0x10000;
        context.flags = translate ? RT_WLOG_FLAG_TRANSLATE : 0;
        context.complete_bt = 0xC0DE;
        context.hooked = 1;
        context.ticks_per_second = 60750000;
        context.stubs = 2;
        context.pro_count = 1;
        for (unsigned i = 0; i < 6; ++i) context.pro_addresses[i] = i + 1;
        if (mode == 0) {
            ioctlv = wlog_full_on_ioctlv;
            after = wlog_full_after_ioctlv;
            complete = wlog_full_on_bt;
            bytes = sizeof(rt_wlog_full_state);
            watches = At<rt_wlog_full_state>(context.state)->watch;
        } else if (mode == 1) {
            ioctlv = wlog_trace_on_ioctlv;
            after = wlog_trace_after_ioctlv;
            complete = wlog_trace_on_bt;
            bytes = sizeof(rt_wlog_trace_state);
            watches = At<rt_wlog_trace_state>(context.state)->watch;
        } else {
            ioctlv = wlog_translate_on_ioctlv;
            after = wlog_translate_after_ioctlv;
            complete = wlog_translate_on_bt;
            bytes = sizeof(rt_wlog_translate_state);
            watches = At<rt_wlog_translate_state>(context.state)->watch;
        }
        std::memset(At<void>(context.state), 0xA5, bytes + 64);
    }

    std::uint32_t Submit(unsigned endpoint, unsigned ioctl, unsigned size, std::uint32_t* args) {
        *At<std::uint8_t>(kEndpoint) = endpoint;
        *At<std::uint16_t>(kLength) = size;
        const std::uint32_t vectors[] = {kEndpoint, 1, kLength, 2, kInput, size};
        std::memcpy(At<void>(kVectors), vectors, sizeof(vectors));
        const std::uint32_t values[] = {7, ioctl, 2, 1, kVectors, 17, 23};
        std::memcpy(args, values, sizeof(values));
        return ioctlv(&context, args);
    }

    std::int32_t Input(const std::vector<std::uint8_t>& packet, unsigned endpoint = 0x81, unsigned room = 64) {
        std::memset(At<void>(kInput), 0xCC, 64);
        std::memcpy(At<void>(kInput), packet.data(), packet.size());
        std::uint32_t args[7];
        const auto slot = Submit(endpoint, endpoint == 0x81 ? 2 : 1, room, args);
        CHECK(slot != 0);
        CHECK(args[5] == context.complete_bt);
        CHECK(args[6] == static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&watches[slot - 1])));
        return complete(&context, packet.size(), args[6]);
    }

    void Output(const std::vector<std::uint8_t>& packet) {
        std::memset(At<void>(kInput), 0xCC, 64);
        std::memcpy(At<void>(kInput), packet.data(), packet.size());
        std::uint32_t args[7];
        CHECK(Submit(2, 1, packet.size(), args) == 0);
    }

    void CheckGuard() const {
        for (unsigned i = 0; i < 64; ++i) CHECK(At<std::uint8_t>(context.state + bytes)[i] == 0xA5);
    }
};

std::vector<std::uint8_t> Connection(unsigned handle, bool reversed = false) {
    std::vector<std::uint8_t> packet = {3, 11, 0, static_cast<std::uint8_t>(handle),
                                      static_cast<std::uint8_t>(handle >> 8), 1, 2, 3, 4, 5, 6, 1, 0};
    if (reversed) std::reverse(packet.begin() + 5, packet.begin() + 11);
    return packet;
}

std::vector<std::uint8_t> Acl(unsigned report, bool input, unsigned payload = 4) {
    std::vector<std::uint8_t> packet(8 + payload, 0);
    packet[0] = 1;
    packet[1] = 0x20;
    packet[2] = payload + 4;
    packet[4] = payload;
    packet[6] = 0x41;
    packet[8] = input ? 0xA1 : 0xA2;
    packet[9] = report;
    return packet;
}

void CompareReports(const Hook& full, const Hook& trace) {
    CHECK(std::memcmp(At<void>(full.context.state), At<void>(trace.context.state), sizeof(rtwlog_file)) == 0);
}

void TestTrace() {
    Hook full(0, false), trace(1, false);
    // Exhaust all seven slots, reuse a handle, disconnect, reconnect,
    // reverse the pairing address, and exercise the special every-link count.
    for (unsigned i = 0; i < 600; ++i) {
        std::vector<std::uint8_t> event;
        switch (i % 8) {
        case 0: event = {5, 4, 0, static_cast<std::uint8_t>(i % 13), 0, 0}; break;
        case 1: event = {3, 11, 0}; break;  // truncated
        case 2: event = {3, 11, 1, 1, 0, 1, 2, 3, 4, 5, 6, 1, 0}; break;
        case 3: event = {0x13, 2, 0, 0}; break;  // counted, never traced
        default: event = Connection(i % 13, i % 2); break;
        }
        if (i == 200) full.context.pro_count = trace.context.pro_count = RTPRO_EVERY_LINK;
        if (i == 400) full.context.pro_count = trace.context.pro_count = RTPRO_ADDRESSES + 10;
        CHECK(full.Input(event) == trace.Input(event));
        CompareReports(full, trace);
        auto packet = Acl(i % 2 ? 0x34 : 0x21, true, 23);
        CHECK(full.Input(packet, 0x82) == trace.Input(packet, 0x82));
        CompareReports(full, trace);
        packet = Acl(i % 2 ? 0x18 : 0x12, false);
        full.Output(packet);
        trace.Output(packet);
        CompareReports(full, trace);
    }
    // Prints must retain exactly the same first/last rings, repeats,
    // copied strings and format conversions as the full image.
    std::strcpy(At<char>(kFormat), "pad %u: %s %% %f %s");
    std::strcpy(At<char>(kString), "connected");
    for (unsigned i = 0; i < 400; ++i) {
        const std::uint32_t words[] = {kFormat, i / 2, kString, kString, 0, 0, 0, 0};
        wlog_full_on_print(&full.context, words);
        wlog_trace_on_print(&trace.context, words);
        CompareReports(full, trace);
    }
    const auto* file = At<rtwlog_file>(trace.context.state);
    CHECK(file->total == 200);
    CHECK(file->entries[0].repeat == 1);
    CHECK(std::strcmp(file->entries[0].strings[0], "connected") == 0);
    CHECK(file->entries[rtwlog_slot(199)].args[0] == 199);
    CHECK(file->pro.data_skipped > 0 && file->pro.events_skipped > 0);
    // Byte-for-byte file writes and all seven IOS failure points agree.
    for (unsigned failure = 0; failure <= 7; ++failure) {
        ios_calls = 0;
        fail_call = failure;
        saved.clear();
        wlog_full_on_exit(&full.context);
        const auto full_saved = saved;
        const auto full_calls = ios_calls;
        ios_calls = 0;
        saved.clear();
        wlog_trace_on_exit(&trace.context);
        CHECK(saved == full_saved);
        CHECK(ios_calls == full_calls);
        CHECK(trace.context.result == full.context.result);
        CompareReports(full, trace);
    }
    fail_call = 0;
    full.CheckGuard();
    trace.CheckGuard();
}

void TestTranslation() {
    Hook full(0, true), translate(2, true);
    CHECK(full.Input(Connection(1)) == translate.Input(Connection(1)));
    for (unsigned mode = 0x30; mode <= 0x3D; ++mode) {
        auto request = Acl(0x12, false);
        request[10] = 4;
        request[11] = mode;
        full.Output(request);
        std::uint8_t expected[64];
        std::memcpy(expected, At<void>(kInput), 64);
        const auto expected_size = At<std::uint32_t>(kVectors)[5];
        translate.Output(request);
        CHECK(std::memcmp(expected, At<void>(kInput), 64) == 0);
        CHECK(At<std::uint32_t>(kVectors)[5] == expected_size);
        auto report = Acl(0x34, true, 23);
        for (unsigned i = 12; i < report.size(); ++i) report[i] = i * 7;
        const auto full_result = full.Input(report, 0x82);
        std::memcpy(expected, At<void>(kInput), 64);
        CHECK(translate.Input(report, 0x82) == full_result);
        CHECK(std::memcmp(expected, At<void>(kInput), 64) == 0);
        CHECK(std::memcmp(&At<rt_wlog_full_state>(full.context.state)->pro,
                          &At<rt_wlog_translate_state>(translate.context.state)->pro, sizeof(rtpro_state)) == 0);
    }
    CHECK(At<rt_wlog_translate_state>(translate.context.state)->pro.translated > 0);
    full.CheckGuard();
    translate.CheckGuard();
}

void TestWatches() {
    for (unsigned mode = 0; mode < 3; ++mode) {
        Hook hook(mode, mode != 1);
        std::uint32_t args[7];
        for (unsigned i = 0; i < RT_WLOG_WATCHES; ++i) {
            CHECK(hook.Submit(0x81, 2, 64, args) == i + 1);
        }
        CHECK(hook.Submit(0x81, 2, 64, args) == 0);
        CHECK(args[5] == 17 && args[6] == 23);
        CHECK(hook.after(&hook.context, 3, -4) == -4);
        CHECK(hook.watches[2].in_use == 0);
        CHECK(hook.Submit(0x82, 1, 64, args) == 3);
        const auto calls = callback_calls;
        CHECK(hook.complete(&hook.context, -9, args[6]) == -9);
        CHECK(callback_calls == calls + 1 && callback_result == -9);
        CHECK(hook.watches[2].in_use == 0);
        CHECK(hook.complete(&hook.context, 1, args[6] + 1) == 0);
        CHECK(hook.complete(&hook.context, 1, 0) == 0);
        CHECK(callback_calls == calls + 1);
        hook.CheckGuard();
    }
}

void TestInstaller() {
    using namespace riftwii;
    using namespace riftwii::wii;
    constexpr std::uint32_t code = 0x80140000;
    constexpr std::uint32_t ioctlv = code + 0x1000;
    constexpr std::uint32_t arena1 = 0x801F0000;
    constexpr std::uint32_t arena2 = 0x90030000;
    *At<std::uint32_t>(0x80003128) = 0x90040000;
    const std::uint32_t ios[] = {1, 2, 3, 4};
    const std::uint32_t no_ios[] = {0, 0, 0, 0};
    DolHeader dol;
    dol.sections[0] = {0x100, code, 0x80, true};
    const std::uint32_t stub[] = {
        0x9421FF90, 0x40860024, 0xD8210028, 0xD8410030, 0xD8610038,
        0xD8810040, 0xD8A10048, 0xD8C10050, 0xD8E10058, 0xD9010060,
        0x90610008, 0x9081000C, 0x90A10010, 0x90C10014, 0x90E10018,
        0x9101001C, 0x91210020, 0x91410024, 0x38210070, 0x4E800020,
        0x38800002, 0x38600001, 0x38A00000
    };
    const auto restore = [&]() {
        std::memset(At<void>(code), 0, 0x80);
        for (unsigned i = 0; i < sizeof(stub) / sizeof(stub[0]); ++i) {
            auto* bytes = At<std::uint8_t>(code + i * 4);
            bytes[0] = stub[i] >> 24;
            bytes[1] = stub[i] >> 16;
            bytes[2] = stub[i] >> 8;
            bytes[3] = stub[i];
        }
        *At<std::uint32_t>(ioctlv) = 0x9421FFE0;
    };
    const auto init_image = [](unsigned char* data, std::uint32_t bytes, bool record) {
        const rt_wlog_header h = {RT_WLOG_MAGIC, RT_WLOG_VERSION, bytes, 64,
                                  record ? 192u : 0u, record ? 208u : 0u, record ? 224u : 0u,
                                  240, 256, 272, 288};
        std::memcpy(data, &h, sizeof(h));
        const std::uint32_t magic = RT_WLOG_CONTEXT_MAGIC;
        std::memcpy(data + 64, &magic, sizeof(magic));
    };
    init_image(riftwii_wlog_bin, riftwii_wlog_bin_size, true);
    init_image(riftwii_wlog_trace_bin, riftwii_wlog_trace_bin_size, true);
    init_image(riftwii_wlog_translate_bin, riftwii_wlog_translate_bin_size, false);
    const std::uint32_t sizes[] = {512, 480, 448};
    for (unsigned mode = 0; mode < 3; ++mode) {
        restore();
        const bool record = mode != 2;
        const bool translate = mode != 1;
        WpadLogHook hook;
        std::string why;
        CHECK(plan_wpad_log(dol, arena1, code + 0x2000, arena2, record ? ios : no_ios,
                            ioctlv, {}, translate, record, {}, hook, why));
        CHECK(hook.active && hook.record == record && hook.translate == translate);
        CHECK(hook.code_bytes == sizes[mode]);
        CHECK(hook.state_bytes == rt_wlog_state_bytes(record, translate));
        CHECK(hook.new_arena2_lo == arena2 + hook.state_bytes);
        CHECK((hook.print_entry != 0) == record);
        auto* context = At<rt_wlog_context>(hook.code_base + 64);
        CHECK(context->state == arena2 && context->flags == (translate ? 1u : 0u));
        // Searches read PowerPC byte order; native load/store hooks operate
        // on words. Replace only the sites after the search on this host.
        *At<std::uint32_t>(code) = 0x9421FF90;
        if (record) *At<std::uint32_t>(hook.exit_site) = 0x38A00000;
        CHECK(install_wpad_log(hook, why));
        CHECK(context->hooked == 1);
        CHECK((*At<std::uint32_t>(ioctlv) & 0xFC000003u) == 0x48000000);
        CHECK(((*At<std::uint32_t>(code) & 0xFC000003u) == 0x48000000) == record);
        CHECK(*At<std::uint32_t>(hook.code_base + 256) == 0x9421FFE0);
        restore();
        MemoryPatch conflict;
        conflict.has_offset = true;
        conflict.offset = arena2;
        conflict.value = {1};
        CHECK(!plan_wpad_log(dol, arena1, code + 0x2000, arena2, ios, ioctlv, {},
                             translate, record, {conflict}, hook, why));
        CHECK(!hook.active);
        CHECK(!plan_wpad_log(dol, arena1, arena1, arena2, ios, ioctlv, {},
                             translate, record, {}, hook, why));
        CHECK(!hook.active);
    }
    restore();
    WpadLogHook hook;
    std::string why;
    CHECK(!plan_wpad_log(dol, arena1, code, arena2, ios, ioctlv, {}, false, false, {}, hook, why));
    CHECK(!hook.active);
    // No print/exit symbols are needed by the translator-only image.
    DolHeader empty;
    CHECK(plan_wpad_log(empty, arena1, code, arena2, no_ios, ioctlv, {}, true, false, {}, hook, why));
    *At<std::uint32_t>(ioctlv) = 0x48000001;  // bl cannot be displaced
    CHECK(!install_wpad_log(hook, why));
    CHECK(!hook.active);
    // The full image still refuses launches whose log could never be saved.
    CHECK(!plan_wpad_log(empty, arena1, code, arena2, ios, ioctlv, {}, true, true, {}, hook, why));
}

}  // namespace

extern "C" std::uint32_t wlog_test_now() { return 1234; }
extern "C" std::int32_t wlog_test_callback(std::uint32_t callback, std::int32_t result, std::uint32_t user) {
    CHECK(callback == 17 && user == 23);
    ++callback_calls;
    callback_result = result;
    return result;
}
extern "C" std::int32_t wlog_test_open(const char* path, std::uint32_t mode) {
    CHECK((mode == 0 && std::strcmp(path, "/dev/fs") == 0) ||
          (mode == 2 && std::strcmp(path, "/shared2/riftwii/wpadlog.bin") == 0));
    return IosResult(7);
}
extern "C" std::int32_t wlog_test_close(std::int32_t fd) {
    CHECK(fd == 7);
    return IosResult(0);
}
extern "C" std::int32_t wlog_test_write(std::int32_t fd, const void* buffer, std::uint32_t bytes) {
    CHECK(fd == 7 && bytes == sizeof(rtwlog_file));
    const auto result = IosResult(bytes);
    if (result >= 0) {
        const auto* begin = static_cast<const std::uint8_t*>(buffer);
        saved.assign(begin, begin + bytes);
    }
    return result;
}
extern "C" std::int32_t wlog_test_ioctl(std::int32_t fd, std::uint32_t command, void* in,
                                        std::uint32_t bytes, void* out, std::uint32_t out_bytes) {
    CHECK(fd == 7 && (command == 3 || command == 9));
    CHECK(in != nullptr && bytes == 76 && out == nullptr && out_bytes == 0);
    return IosResult(0);
}

int main() {
    if (FixedBuffer(0x80000000, 0x200000) == nullptr || FixedBuffer(0x90000000, 0x40000) == nullptr) {
        std::cout << "Wii address ranges unavailable\n";
        return 77;
    }
    std::memcpy(At<void>(0x80000000), "TEST01\0\0", 8);
    CHECK(rt_wlog_state_bytes(false, false) == 0);
    CHECK(rt_wlog_state_bytes(true, false) < rt_wlog_state_bytes(true, true));
    CHECK(rt_wlog_state_bytes(false, true) < 2048);
    TestTrace();
    TestTranslation();
    TestWatches();
    TestInstaller();
    return failures == 0 ? 0 : 1;
}

namespace riftwii::wii {
void logf(const char*, ...) {}
bool HidPadsConnected() { return false; }
bool pro_translator_every_link() { return false; }
}  // namespace riftwii::wii
