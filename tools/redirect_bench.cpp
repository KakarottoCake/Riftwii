// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only timing of the phase-1 walker and the compact/wide replacement.
// Both C walkers are compiled with -Os, as in the resident runtime.
#include "rtable.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

extern "C" int legacy_lookup(const rt_header*, std::uint64_t, std::uint64_t,
                             rt_run*, std::uint32_t, std::uint32_t*);

using Lookup = int (*)(const rt_header*, std::uint64_t, std::uint64_t,
                      rt_run*, std::uint32_t, std::uint32_t*);
using Bytes = std::vector<std::uint8_t>;
static volatile std::uint64_t result_sink;

static Bytes MakeTable(const std::vector<rt_entry>& entries, std::uint32_t format) {
    rt_header header{};
    header.magic = RT_MAGIC;
    header.version = RT_VERSION;
    header.entry_count = static_cast<std::uint32_t>(entries.size());
    header.entry_bytes = format;
    Bytes bytes(rt_table_bytes(header.entry_count, format));
    std::memcpy(bytes.data(), &header, sizeof(header));
    rt_encode_entries(reinterpret_cast<rt_header*>(bytes.data()), entries.data());
    return bytes;
}

static double Measure(Lookup lookup, const Bytes& bytes, const std::vector<std::uint64_t>& offsets,
                      std::uint64_t length) {
    const auto* table = reinterpret_cast<const rt_header*>(bytes.data());
    constexpr std::uint32_t repetitions = 64;
    std::uint64_t checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint32_t repeat = 0; repeat < repetitions; ++repeat) {
        for (const std::uint64_t offset : offsets) {
            rt_run runs[8];
            std::uint32_t count;
            const int status = lookup(table, offset, length, runs, 8, &count);
            checksum += count + status;
            for (std::uint32_t i = 0; i < count; ++i) {
                checksum += runs[i].source + runs[i].skip + runs[i].kind + runs[i].length;
            }
        }
    }
    const auto end = std::chrono::steady_clock::now();
    result_sink = checksum;
    const double seconds = std::chrono::duration<double>(end - start).count();
    return double(repetitions) * offsets.size() / seconds;
}

int main() {
    std::cout << "Entries,pattern,old/s,compact/s,wide/s,compact speedup,wide speedup\n";
    for (const std::uint32_t count : {1000u, 10000u}) {
        std::vector<rt_entry> entries(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            rt_entry& entry = entries[i];
            entry.vstart = 0x100000000ull + std::uint64_t(i) * 4096 + (i % 4);
            entry.length = 3072;
            entry.kind = 1 + i % 5;
            entry.source = entry.kind == RT_KIND_ZERO ? 0 : 0x80000000u + i * 8;
            entry.skip = entry.kind == RT_KIND_SD || entry.kind == RT_KIND_USB ? i % 512 : 0;
        }
        const Bytes wide = MakeTable(entries, RT_ENTRY_WIDE);
        const Bytes compact = MakeTable(entries, RT_ENTRY_COMPACT);
        if (rt_validate(reinterpret_cast<const rt_header*>(compact.data()), compact.size()) != RT_OK) return 1;
        struct Pattern {
            const char* name;
            std::uint64_t length;
            std::uint32_t offset_in_entry;
            bool sequential;
        };
        const Pattern patterns[] = {
            {"random hit", 64, 100, false},
            {"sequential hit", 64, 100, true},
            {"random split", 8192, 3000, false},
            {"random gap", 64, 3500, false},
        };
        for (const Pattern& pattern : patterns) {
            std::vector<std::uint64_t> offsets(16384);
            std::uint32_t seed = 0x12345678;
            for (std::size_t i = 0; i < offsets.size(); ++i) {
                seed = seed * 1664525u + 1013904223u;
                const std::uint32_t index = pattern.sequential ? i % count : seed % count;
                offsets[i] = entries[index].vstart + pattern.offset_in_entry;
            }
            std::vector<double> old_rates, compact_rates, wide_rates;
            // Rotate measurement order and report medians to reduce warm-up
            // and scheduling bias. Timing never acts as a flaky unit test.
            for (int sample = 0; sample < 7; ++sample) {
                for (int turn = 0; turn < 3; ++turn) {
                    switch ((sample + turn) % 3) {
                    case 0:
                        old_rates.push_back(Measure(legacy_lookup, wide, offsets, pattern.length));
                        break;
                    case 1:
                        compact_rates.push_back(Measure(rt_lookup, compact, offsets, pattern.length));
                        break;
                    case 2:
                        wide_rates.push_back(Measure(rt_lookup, wide, offsets, pattern.length));
                        break;
                    }
                }
            }
            std::sort(old_rates.begin(), old_rates.end());
            std::sort(compact_rates.begin(), compact_rates.end());
            std::sort(wide_rates.begin(), wide_rates.end());
            const double old_rate = old_rates[3];
            const double compact_rate = compact_rates[3];
            const double wide_rate = wide_rates[3];
            std::cout << count << ',' << pattern.name << ',' << std::fixed << std::setprecision(0)
                      << old_rate << ',' << compact_rate << ',' << wide_rate << ','
                      << std::setprecision(3) << compact_rate / old_rate << ',' << wide_rate / old_rate << '\n';
        }
    }
}
