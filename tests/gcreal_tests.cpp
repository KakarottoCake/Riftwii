// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcreal.hpp"
#include "riftwii/gccard.hpp"
#include "riftwii/gcbackup.hpp"
#include "riftwii/launch.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace riftwii;
using Bytes = std::vector<std::uint8_t>;
static unsigned failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Line " << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

static Bytes card(bool japanese = false, unsigned mbits = 16) {
    GcCardFormat format;
    format.shift_jis = japanese;
    format.size_mbits = static_cast<std::uint16_t>(mbits);
    Bytes image;
    gc_card_format(image, format);
    return image;
}

static Bytes save(const std::string& id, const std::string& name, unsigned blocks = 1) {
    Bytes gci(64 + blocks * kGcCardBlockBytes, 0);
    std::copy_n(id.begin(), 6, gci.begin());
    std::copy(name.begin(), name.end(), gci.begin() + 8);
    gci[0x38] = static_cast<std::uint8_t>(blocks >> 8);
    gci[0x39] = static_cast<std::uint8_t>(blocks);
    return gci;
}

static void dma_addressing() {
    CHECK(gc_real_dma_range(0x81000000, 5 * 8192));
    CHECK(gc_real_dma_range(0x817FFE00, 512));
    CHECK(!gc_real_dma_range(0x817FFE00, 5 * 8192));
    CHECK(!gc_real_dma_range(0x81800000, 512));
    CHECK(!gc_real_dma_range(0, 512));
    CHECK(!gc_real_dma_range(0x81000001, 512));
    CHECK(!gc_real_dma_range(0x81000000, 0));
    CHECK(!gc_real_dma_range(0x81000000, 511));
    CHECK(!gc_real_dma_range(0x81000000, static_cast<std::size_t>(-1)));

    // The old heap allocation could be here after the menu filled MEM1.
    // EXI would DMA into 0x80800000, overwriting unrelated menu memory.
    constexpr std::uintptr_t mem2_buffer = 0x90800000;
    constexpr std::uintptr_t dma_alias = 0x80000000 | (mem2_buffer & 0x03FFFFE0);
    CHECK(dma_alias == 0x80800000);
    CHECK(!gc_real_dma_range(mem2_buffer, 5 * 8192));
    CHECK(!gc_real_dma_range(mem2_buffer, 512));
    CHECK(!gc_real_dma_range(0xD0800000, 512));
}

static void foreign_identity() {
    GcCardFormat format;
    format.size_mbits = 16;
    format.format_time = 123456789;
    format.flash_id = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    Bytes source;
    gc_card_format(source, format);
    std::string error;
    CHECK(gc_card_import(source, save("GALE01", "foreign console"), error));
    const Bytes before = source;
    GcRealPlan plan;
    CHECK(gc_real_plan(source, card(), "GALE01", false, plan, error));
    CHECK(plan.selected == 1 && plan.count.added == 1);
    CHECK(source == before);

    // Launch preflight reads only these five blocks, with the capacity
    // from the probe. Neither path compares the format identity with SRAM.
    const Bytes system(source.begin(), source.begin() + 5 * kGcCardBlockBytes);
    GcCardSpace space;
    CHECK(gc_card_space(system, source.size(), space, error));
    std::array<std::uint8_t, 12> recovered{};
    CHECK(gc_card_flash_id(system, recovered));
    CHECK(recovered == format.flash_id);
}

static void planning() {
    Bytes source = card();
    std::string error;
    CHECK(gc_card_import(source, save("GALE01", "same"), error));
    CHECK(gc_card_import(source, save("GALE02", "other maker"), error));
    CHECK(gc_card_import(source, save("GZLE01", "other game"), error));
    CHECK(gc_card_import(source, save("GALE01", "second"), error));
    const Bytes original = card();
    GcRealPlan plan;
    CHECK(gc_real_plan(source, original, "GALE01", false, plan, error));
    CHECK(plan.selected == 2 && plan.count.added == 2);
    CHECK(original == card());
    Bytes gci;
    CHECK(gc_card_export(plan.image, 0, gci, error));
    Bytes exported;
    CHECK(gc_card_export(source, 0, exported, error));
    CHECK(gci == exported);
    Bytes target = original;
    CHECK(gc_card_import(target, save("GALE01", "same"), error));
    CHECK(gc_real_plan(source, target, "GALE01", false, plan, error));
    CHECK(plan.count.added == 1 && plan.count.skipped == 1);
    CHECK(gc_real_plan(source, original, "GALE01", true, plan, error));
    CHECK(plan.selected == 4 && plan.count.added == 4);
    CHECK(gc_real_plan(source, original, "NONE00", false, plan, error));
    CHECK(plan.selected == 0 && plan.image == original);
    CHECK(!gc_real_plan(source, card(true), "GALE01", true, plan, error));
    CHECK(plan.image.empty());
    CHECK(!gc_real_plan(Bytes(100), original, "GALE01", true, plan, error));
    CHECK(!gc_real_plan(source, Bytes(100), "GALE01", true, plan, error));
    CHECK(!gc_real_plan(source, original, "bad", false, plan, error));
    CHECK(!gc_real_plan(Bytes((16u << 20) + 1), original, "GALE01", true, plan, error));

    target = card(false, 4);
    CHECK(gc_card_import(target, save("GALE01", "large", 59), error));
    CHECK(gc_real_plan(source, target, "GALE01", true, plan, error));
    CHECK(plan.count.full && plan.count.added == 0 && plan.image == target);
    target = card();
    for (unsigned i = 0; i < 127; ++i)
        CHECK(gc_card_import(target, save("GALE01", "slot" + std::to_string(i)), error));
    CHECK(gc_real_plan(source, target, "GALE01", true, plan, error));
    CHECK(plan.count.full && plan.count.added == 0 && plan.image == target);

    // A checksum-valid directory whose block chain is damaged is unusable.
    source = card();
    CHECK(gc_card_import(source, save("GALE01", "broken"), error));
    GcCardInfo info;
    CHECK(gc_card_read(source, info, error));
    const unsigned bat = (3 + info.bat_copy) * kGcCardBlockBytes;
    source[bat + 10] = 0;
    source[bat + 11] = 5;  // block 5 points to itself
    gc_card_checksum(source.data() + bat + 4, 0x1FFC, source.data() + bat);
    CHECK(gc_real_plan(source, original, "GALE01", false, plan, error));
    CHECK(plan.count.unusable == 1 && plan.count.added == 0);

    LaunchModel settings;
    settings.game.gc_card = "real";
    LaunchModel restored;
    restored.restore(settings.save());
    CHECK(restored.game.gc_card == "real");
}

static Bytes read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static void transaction() {
    const auto folder = std::filesystem::temp_directory_path() / "riftwii-real-card-tests";
    std::filesystem::create_directories(folder);
    const std::string path = (folder / "card.raw").string();
    const Bytes before = card();
    Bytes after = before;
    std::string error;
    CHECK(gc_card_import(after, save("GALE01", "new save"), error));
    CHECK(gc_overwrite_file(path, before, error));
    bool checked_after_staging = false;
    CHECK(!gc_real_commit(path, after, [&] {
        checked_after_staging = read_file(path + ".real-new") == after;
        return false;
    }, error));
    CHECK(checked_after_staging);
    CHECK(read_file(path) == before);
    CHECK(!std::filesystem::exists(path + ".real-new"));
    CHECK(gc_real_commit(path, after, [] { return true; }, error));
    CHECK(read_file(path) == after);
    CHECK(!std::filesystem::exists(path + ".old"));
    std::filesystem::remove(path);
    CHECK(!gc_real_commit(path, after, [] { return false; }, error));
    CHECK(!std::filesystem::exists(path));
    CHECK(gc_real_commit(path, after, [] { return true; }, error));
    CHECK(read_file(path) == after);
    // An unresolved recovery copy must never be overwritten.
    CHECK(gc_overwrite_file(path + ".old", before, error));
    CHECK(!gc_real_commit(path, before, [] { return true; }, error));
    CHECK(read_file(path) == after && read_file(path + ".old") == before);
    std::filesystem::remove(path + ".old");
    std::filesystem::remove(path);
    std::filesystem::remove(folder);
}

// The Wii has no exceptions: when memory is short the plan and the commit
// must say no, and the commit must leave the destination untouched.
static void refuses_without_memory() {
    static int calls_allowed;
    const auto nth_fails = [](std::size_t bytes) -> void* {
        return calls_allowed-- > 0 ? std::malloc(bytes) : nullptr;
    };
    calls_allowed = 5;
    CHECK(gc_real_have_room({100, 200, 300}));
    CHECK(gc_real_have_room({100, 200, 300}, nth_fails));
    calls_allowed = 2;
    CHECK(!gc_real_have_room({100, 200, 300}, nth_fails));  // the third says no; the first two are freed

    const Bytes source = card();
    const Bytes target = card();
    GcRealPlan plan;
    std::string error;
    calls_allowed = 0;
    CHECK(!gc_real_plan(source, target, "GALE01", true, plan, error, nth_fails));
    CHECK(plan.out_of_memory && plan.image.empty());
    calls_allowed = 100;
    CHECK(gc_real_plan(source, target, "GALE01", true, plan, error, nth_fails));
    CHECK(!plan.out_of_memory);

    const auto folder = std::filesystem::temp_directory_path() / "riftwii-real-card-memory-tests";
    std::filesystem::create_directories(folder);
    const std::string path = (folder / "card.raw").string();
    CHECK(gc_overwrite_file(path, source, error));
    Bytes changed = source;
    changed[100] ^= 1;
    calls_allowed = 0;
    CHECK(!gc_real_commit(path, changed, [] { return true; }, error, nth_fails));
    CHECK(read_file(path) == source && !std::filesystem::exists(path + ".real-new"));
    std::filesystem::remove(path);
    std::filesystem::remove(folder);
}

int main() {
    dma_addressing();
    refuses_without_memory();
    foreign_identity();
    planning();
    transaction();
    return failures == 0 ? 0 : 1;
}
