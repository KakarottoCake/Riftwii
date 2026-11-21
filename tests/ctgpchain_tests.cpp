// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The rules of a chain of CTGP-R updates (riftwii/ctgpchain.hpp), against a
// made-up server that offers one update at a time.
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgpchain.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

struct Offer {
    std::string component;
    std::uint32_t version;
};

struct Card {
    std::uint32_t ctgpr = 0x01031192u;
    std::uint32_t hash = 0x01031374u;
    std::uint32_t& of(const std::string& c) { return c == "HASH" ? hash : ctgpr; }
};

// Runs a chain against `server` (it answers each check by what the card
// holds); `fail_at` (1-based, 0: none) is a step that fails. Returns how it
// stopped.
std::string run(const std::vector<Offer>& server, Card& card, CtgpChain& chain, std::size_t fail_at = 0,
                bool server_ignores_card = false) {
    std::size_t cursor = 0;
    for (;;) {
        // The server's next offer: the first one newer than the card has.
        const Offer* next = nullptr;
        for (std::size_t i = server_ignores_card ? cursor : 0; i < server.size(); ++i) {
            const Offer& o = server[i];
            if (server_ignores_card || o.component == "CORE" || o.version > card.of(o.component)) {
                next = &o;
                cursor = i + 1;
                break;
            }
        }
        if (server_ignores_card && next == nullptr && !server.empty()) next = &server.back();
        if (next == nullptr) return "current";
        const std::uint32_t installed = next->component == "CORE" ? 0x01030412u : card.of(next->component);
        switch (chain.offer(next->component, next->version, installed)) {
        case CtgpChainVerdict::Take: break;
        case CtgpChainVerdict::NotOurs: return "not ours";
        case CtgpChainVerdict::NotNewer: return "not newer";
        case CtgpChainVerdict::Repeat: return "repeat";
        case CtgpChainVerdict::TooManySteps: return "too many";
        }
        if (chain.next_number() == fail_at) return "failed";
        chain.installed(next->component, installed, next->version);
        card.of(next->component) = next->version;
    }
}

}  // namespace

int main() {
    // The channel's own chain from the stock pack: five steps, HASH and
    // CTGPR mixed, ends current.
    const std::vector<Offer> server = {{"CTGPR", 0x01031193u}, {"CTGPR", 0x01031194u}, {"HASH", 0x01031376u},
                                       {"CTGPR", 0x01031195u}, {"CTGPR", 0x01031196u}};
    {
        Card card;
        CtgpChain chain;
        EXPECT_TRUE(run(server, card, chain) == "current");
        EXPECT_TRUE(chain.steps().size() == 5);
        EXPECT_TRUE(card.ctgpr == 0x01031196u && card.hash == 0x01031376u);
        EXPECT_TRUE(chain.ctgpr_from() == 0x01031192u && chain.ctgpr_to() == 0x01031196u);
        EXPECT_TRUE(chain.steps()[2].component == "HASH" && chain.steps()[2].from == 0x01031374u);
    }
    {
        // A failed third step: two installed, the card keeps them.
        Card card;
        CtgpChain chain;
        EXPECT_TRUE(run(server, card, chain, 3) == "failed");
        EXPECT_TRUE(chain.steps().size() == 2 && card.ctgpr == 0x01031194u && chain.ctgpr_to() == 0x01031194u);
    }
    {
        // Already current: nothing offered, no steps.
        Card card;
        card.ctgpr = 0x01031196u;
        card.hash = 0x01031376u;
        CtgpChain chain;
        EXPECT_TRUE(run(server, card, chain) == "current" && chain.steps().empty() && chain.ctgpr_from() == 0);
    }
    {
        // Another package: stop and say so.
        Card card;
        CtgpChain chain;
        EXPECT_TRUE(run({{"CTGPR", 0x01031193u}, {"CORE", 0x01030413u}}, card, chain) == "not ours");
        EXPECT_TRUE(chain.steps().size() == 1);
    }
    {
        // A server that offers the same update again although it went in.
        Card card;
        CtgpChain chain;
        EXPECT_TRUE(run({{"CTGPR", 0x01031193u}}, card, chain, 0, true) == "repeat");
        EXPECT_TRUE(chain.steps().size() == 1);
    }
    {
        // An offer older than what is installed.
        CtgpChain chain;
        EXPECT_TRUE(chain.offer("CTGPR", 0x01031192u, 0x01031193u) == CtgpChainVerdict::NotNewer);
        EXPECT_TRUE(chain.offer("HASH", 0x01031374u, 0x01031374u) == CtgpChainVerdict::NotNewer);
        // Unknown installed version (0): the version can't be compared, the
        // rest still holds.
        EXPECT_TRUE(chain.offer("CTGPR", 0x01031193u, 0) == CtgpChainVerdict::Take);
    }
    {
        // An endless run of ever newer updates: stops after the cap.
        std::vector<Offer> endless;
        for (std::uint32_t v = 0x01031193u; v < 0x01031193u + 40; ++v) endless.push_back({"CTGPR", v});
        Card card;
        CtgpChain chain;
        EXPECT_TRUE(run(endless, card, chain) == "too many");
        EXPECT_TRUE(chain.steps().size() == kCtgpChainMaxSteps);
    }
    if (g_failures == 0) std::cout << "ctgpchain: all tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
