// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Frozen pre-rewrite code is the oracle for reports, files and IOS jobs.
#include "rtshot.h"
#include "rtfault.h"
#include "fixtures/small-blobs/legacy_rtshot.h"
#include "fixtures/small-blobs/legacy_rtfault.h"
#include "smallblob_model.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#elif defined(__linux__)
#include <sys/mman.h>
#endif

namespace {
int failures = 0;
void check(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "Failed: " << label << '\n';
        ++failures;
    }
}
std::uint32_t seed = 0x53484f54;
std::uint32_t random_word() {
    seed = seed * 1664525u + 1013904223u;
    return seed;
}
void test_reports() {
    rtshot_input current{};
    legacy_rtshot_input old{};
    for (unsigned i = 0; i < 20000; ++i) {
        const unsigned handle = random_word() % 7;
        const unsigned id = (i % 17 == 0) ? random_word() % 256 : 0x30 + i % 8;
        std::array<std::uint8_t, 20> report{};
        report[0] = handle;
        report[1] = 0x20;
        report[2] = 12;
        report[4] = 8;
        report[6] = 0x41;
        report[8] = 0xa1;
        report[9] = id;
        report[10] = random_word() >> 24;
        report[11] = random_word() >> 24;
        if (i % 31 == 0) report[random_word() % 10] = random_word() >> 24;
        auto previous_report = report;
        const unsigned size = i % 23 == 0 ? i % 20 : 20;
        const int fired = rtshot_acl(&current, report.data(), size);
        const int previous_fired = legacy_rtshot_acl(&old, previous_report.data(), size);
        check(fired == previous_fired && report == previous_report && current.reports == old.reports,
              "Wii Remote report and swallowed HOME match");
    }
    // Report IDs outside the usual continuous mode, including status replies.
    for (unsigned id = 0; id < 256; ++id) {
        rtshot_input fresh{};
        legacy_rtshot_input old_fresh{};
        for (unsigned buttons : {2u, 0x82u, 0x80u, 0u}) {
            std::array<std::uint8_t, 12> report{0, 0x20, 8, 0, 4, 0, 0x41, 0, 0xa1,
                                                 static_cast<std::uint8_t>(id), 0,
                                                 static_cast<std::uint8_t>(buttons)};
            auto old_report = report;
            check(rtshot_acl(&fresh, report.data(), report.size()) ==
                      legacy_rtshot_acl(&old_fresh, old_report.data(), old_report.size()) && report == old_report,
                  "All report IDs match");
        }
    }
    for (unsigned i = 0; i < 10000; ++i) {
        std::array<std::uint8_t, 48> pads{};
        for (auto& byte : pads) byte = random_word() >> 24;
        for (unsigned port = 0; port < 4; ++port) pads[port * 12 + 10] = i % 7 ? 0 : -1;
        auto old_pads = pads;
        check(rtshot_pads(&current, pads.data()) == legacy_rtshot_pads(&old, old_pads.data()) && pads == old_pads,
              "GameCube buttons and masking match");
    }
}
void test_file_bytes() {
    for (unsigned i = 0; i < 10000; ++i) {
        struct rtshot_frame frame{};
        struct legacy_rtshot_frame old_frame{};
        const auto vtr = static_cast<std::uint16_t>(random_word());
        const auto picture = static_cast<std::uint16_t>(random_word());
        const auto address = random_word();
        check(rtshot_frame(vtr, picture, address, &frame) ==
                  legacy_rtshot_frame(vtr, picture, address, &old_frame) &&
                  std::memcmp(&frame, &old_frame, sizeof(frame)) == 0, "Frame description matches");
        std::array<std::uint8_t, 64> header{}, old_header{};
        std::array<std::uint8_t, 8> game{};
        for (auto& byte : game) byte = random_word() >> 24;
        const auto number = random_word();
        const auto time = random_word();
        rtshot_header(header.data(), &frame, game.data(), number, time);
        legacy_rtshot_header(old_header.data(), &old_frame, game.data(), number, time);
        check(header == old_header, "Every screenshot header byte matches");
        std::array<char, 64> path{}, old_path{};
        rtshot_path(path.data(), number);
        legacy_rtshot_path(old_path.data(), number);
        check(path == old_path, "Final filename matches");
        rtshot_temp_path(path.data(), number);
        legacy_rtshot_temp_path(old_path.data(), number);
        check(path == old_path, "Temporary filename matches");
        std::array<std::uint8_t, 76> attr{}, old_attr{};
        rtshot_fs_attr(attr.data(), path.data());
        legacy_rtshot_fs_attr(old_attr.data(), old_path.data());
        check(attr == old_attr, "Every attribute byte matches");
    }
}
void test_crash_bytes(void* memory) {
    auto* words = static_cast<std::uint32_t*>(memory);
    const auto address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory));
    for (unsigned i = 0; i < 128; ++i) words[i] = random_word();
    for (unsigned count = 0; count <= 25; ++count) {
        std::uint32_t context[0x68];
        for (auto& word : context) word = random_word();
        context[1] = address + 0x100;
        context[RTFAULT_CTX_SRR0] = address + 16;
        for (unsigned i = 0; i < 26; ++i) {
            words[64 + 2 * i] = i < count ? address + 0x100 + 8 * (i + 1) : 0;
            words[65 + 2 * i] = random_word();
        }
        for (unsigned exception = 0; exception < 16; ++exception) {
            for (int memory_ok : {0, 1}) {
                rtfault_record record;
                legacy_rtfault_record old_record;
                std::memset(&record, 0xa5, sizeof(record));
                std::memset(&old_record, 0x5a, sizeof(old_record));
                rtfault_fill(&record, exception, context, 1, 2, "RMCE01\0\2", 3, 4, 60750000, memory_ok);
                legacy_rtfault_fill(&old_record, exception, context, 1, 2, "RMCE01\0\2", 3, 4, 60750000, memory_ok);
                check(std::memcmp(&record, &old_record, sizeof(record)) == 0, "All 512 crash bytes match");
                check(rtfault_is_crash(exception, context[RTFAULT_CTX_SRR1]) ==
                          legacy_rtfault_is_crash(exception, context[RTFAULT_CTX_SRR1]), "Crash selection matches");
            }
        }
    }
}
void test_jobs(void* memory) {
    shot_model_trace current{}, old{};
    auto compare = [&](const shot_model_script& script) {
        shot_model_run(memory, &script, &current);
        legacy_shot_model_run(memory, &script, &old);
        if (std::memcmp(&current, &old, sizeof(current)) != 0) {
            std::cerr << "Job: request " << script.failure_at << ", result " << script.failure
                      << ", immediate " << script.immediate << ", collisions " << script.probe_taken
                      << '/' << script.part_taken << '\n';
            check(false, "IOS requests, written bytes and final result match");
        }
    };
    compare({});
    check(current.shots == 1 && current.failures == 0, "Successful job publishes one shot");
    // Every submitted phase: refusal, asynchronous error, and short write.
    for (unsigned phase = 1; phase <= 11; ++phase) {
        for (int result : {-7, -105, -106, 0, 1, 63}) {
            compare({phase, result, 0, 0, 0});
            if (result < 0) compare({phase, result, 1, 0, 0});
        }
    }
    for (unsigned taken : {1u, 3u, 98u, 99u, 100u}) {
        compare({0, 0, 0, taken, 0});
        compare({0, 0, 0, 0, taken});
        compare({0, 0, 0, taken, 3});
    }
}
void test_crash_save(void* memory) {
    fault_model_trace current{}, old{};
    for (unsigned request = 0; request <= 7; ++request) {
        for (int result : {-105, -7, 0, 1, 511, 512}) {
            fault_model_run(memory, request, result, &current);
            legacy_fault_model_run(memory, request, result, &old);
            check(std::memcmp(&current, &old, sizeof(current)) == 0,
                  "Crash save requests, record bytes, errors and bounded BCA answer match");
        }
    }
}
void test_stub() {
    stub_model_trace current{}, old{};
    auto compare = [&](const stub_model_script& script) {
        stub_model_run(&script, &current);
        legacy_stub_model_run(&script, &old);
        check(std::memcmp(&current, &old, sizeof(current)) == 0,
              "Return title order, IPC bytes, timeout and restart match");
    };
    for (unsigned count : {0u, 1u, 2u, 3u, 4u, 0xffffffffu}) {
        for (unsigned accept = 0; accept < 5; ++accept) {
            compare({count, accept, 0, 0, {7, -1, -2, -3, 8}});
            compare({count, accept, 0, 3, {7, -1, -2, -3, 8}});
        }
    }
    for (unsigned request = 1; request <= 5; ++request) {
        compare({2, 0, request, 0, {7, -1, -2, 8, 0}});
    }
    compare({3, 0, 0, 0, {-1, -2, 0, 0, 0}});
    compare({0, 0, 0, 64, {7, 8, 0, 0, 0}});
}
void* map_memory() {
    void* address = reinterpret_cast<void*>(std::uintptr_t{0x91000000u});
#if defined(_WIN32) || defined(__CYGWIN__)
    return VirtualAlloc(address, 0x20000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#elif defined(__linux__)
    void* region = mmap(address, 0x20000, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (region == MAP_FAILED) return nullptr;
    if (region != address) {
        munmap(region, 0x20000);
        return nullptr;
    }
    return region;
#else
    return nullptr;
#endif
}
}
int main() {
    test_reports();
    test_file_bytes();
    test_stub();
    void* memory = map_memory();
    if (!memory) return failures ? 1 : 77;
    test_crash_bytes(memory);
    test_crash_save(memory);
    test_jobs(memory);
#if defined(_WIN32) || defined(__CYGWIN__)
    VirtualFree(memory, 0, MEM_RELEASE);
#elif defined(__linux__)
    munmap(memory, 0x20000);
#endif
    return failures ? 1 : 0;
}
