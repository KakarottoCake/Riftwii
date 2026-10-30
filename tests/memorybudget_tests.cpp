// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <iostream>

#include "gcadapter.hpp"
#include "loadersettings.hpp"
#include "memorybudget.hpp"
#include "memorybudget_blobs.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

namespace riftwii::wii {
static LoaderSettings settings;
static bool adapter_plugged = false;

LoaderSettings& Settings() { return settings; }
const char* tr(const char* english) { return english; }
bool GcAdapterMenuLastView(GcAdapterView& view) {
    view.open = adapter_plugged;
    view.link = adapter_plugged ? GCAD_LINK_POLL : GCAD_LINK_NONE;
    return true;
}
}  // namespace riftwii::wii

using namespace riftwii;
using namespace riftwii::wii;

static void test_adapter_budget() {
    for (bool usb : {false, true}) {
        for (UsbImageFormat format : {UsbImageFormat::Iso, UsbImageFormat::Wbfs, UsbImageFormat::Rvz}) {
            FrontendState state;
            state.game_id = "RMCE01";
            state.model.save_mode = "nand";
            state.use_usb = usb;
            state.use_sd = !usb;
            ImageGame game;
            game.format = format;
            if (usb) state.usb_catalog.games.push_back(game);
            else state.sd_catalog.games.push_back(game);

            settings.gc_adapter = "off";
            const ExtrasBudget without_adapter = EstimateExtras(state);
            settings.gc_adapter = "on";
            const ExtrasBudget with_adapter = EstimateExtras(state);
            const auto state_bytes = (sizeof(gcad_hub) + 31) & ~std::size_t(31);
            EXPECT_EQ(with_adapter.mem1() - without_adapter.mem1(), budget_pad.size);
            EXPECT_EQ(with_adapter.mem2() - without_adapter.mem2(), state_bytes);
            EXPECT_EQ(with_adapter.pieces.size(), without_adapter.pieces.size() + 1);

            settings.gc_adapter = "auto";
            adapter_plugged = false;
            EXPECT_EQ(EstimateExtras(state).total(), without_adapter.total());
            adapter_plugged = true;
            EXPECT_EQ(EstimateExtras(state).total(), with_adapter.total());
            if (format == UsbImageFormat::Rvz) {
                EXPECT_TRUE(with_adapter.list().find("RVZ and mods") != std::string::npos);
                EXPECT_TRUE(with_adapter.list().find("crash recorder") != std::string::npos);
                EXPECT_TRUE(with_adapter.list().find("GameCube adapter") != std::string::npos);
                EXPECT_TRUE(with_adapter.mem1() >= 160 * 1024 + budget_pad.size + budget_fault.size);
            }
        }
    }
}

// A code build keeps all of MEM1, so the adapter's code and state both go
// to MEM2 (wii/hooks/padhook.cpp's veneers): the estimate must say so.
static void test_code_build_adapter_budget() {
    for (UsbImageFormat format : {UsbImageFormat::Wbfs, UsbImageFormat::Rvz}) {
        FrontendState state;
        state.game_id = "RSBE01";
        state.model.save_mode = "nand";
        state.use_sd = true;
        ImageGame game;
        game.format = format;
        state.sd_catalog.games.push_back(game);
        LaunchPackage build;
        build.enabled = true;
        build.gct_path = "sd:/codes/RSBE01.gct";
        state.model.packages.push_back(build);

        adapter_plugged = false;
        settings.gc_adapter = "off";
        const ExtrasBudget without_adapter = EstimateExtras(state);
        settings.gc_adapter = "on";
        const ExtrasBudget with_adapter = EstimateExtras(state);
        const auto state_bytes = (sizeof(gcad_hub) + 31) & ~std::size_t(31);
        EXPECT_EQ(with_adapter.mem1(), without_adapter.mem1());
        EXPECT_EQ(with_adapter.mem2() - without_adapter.mem2(), budget_pad.size + state_bytes);
        EXPECT_TRUE(with_adapter.list().find("GameCube adapter") != std::string::npos);
    }
}

int main() {
    test_adapter_budget();
    test_code_build_adapter_budget();
    if (g_failures != 0) return 1;
    std::cout << "ALL MEMORYBUDGET TESTS PASSED" << std::endl;
    return 0;
}
