// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/cardlog.hpp"

#include <cstdarg>
#include <cstdio>

namespace riftwii {
namespace {

constexpr double kTicksPerSecond = 60750000.0;  // the Wii's time base: the 243 MHz bus / 4

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

std::string format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string format(const char* fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

const char* kind_name(unsigned kind) {
    switch (kind) {
    case 1: return "refused by IOS";
    case 2: return "failed";
    case 3: return "status check failed";
    case 4: return "card still busy when the wait gave up";
    case 5: return "card reported a write error";
    case 6: return "gave up, the error reached the game";
    case 7: return "mod file read failed";
    default: return "unknown event";
    }
}

// The card's state (R1 bits 9-12) and error bits, per the SD spec.
std::string status_text(std::uint32_t status) {
    static const char* const kStates[] = {"idle", "ready", "ident", "stby", "tran", "data", "rcv", "prg", "dis"};
    const unsigned state = (status >> 9) & 15u;
    std::string text = format("0x%08x (%s", static_cast<unsigned>(status), state < 9 ? kStates[state] : "state ?");
    static const struct {
        unsigned bit;
        const char* name;
    } kErrors[] = {{31, "OUT_OF_RANGE"},   {30, "ADDRESS_ERROR"},   {29, "BLOCK_LEN_ERROR"}, {28, "ERASE_SEQ_ERROR"},
                   {27, "ERASE_PARAM"},    {26, "WP_VIOLATION"},    {25, "CARD_IS_LOCKED"},  {24, "LOCK_UNLOCK_FAILED"},
                   {23, "COM_CRC_ERROR"},  {22, "ILLEGAL_COMMAND"}, {21, "CARD_ECC_FAILED"}, {20, "CC_ERROR"},
                   {19, "ERROR"}};
    for (const auto& e : kErrors) {
        if (status & (1u << e.bit)) text += std::string(", ") + e.name;
    }
    return text + ")";
}

}  // namespace

bool parse_card_log(const std::uint8_t* bytes, std::size_t size, CardLog& out) {
    if (bytes == nullptr || size < kCardLogBytes) return false;
    if (be32(bytes) != kCardLogMagic || be32(bytes + 4) != kCardLogVersion) return false;
    CardLog log;
    log.events = be32(bytes + 8);
    log.path = be32(bytes + 12);
    log.transfers = be32(bytes + 16);
    log.failures = be32(bytes + 20);
    log.retries = be32(bytes + 24);
    log.settle_polls = be32(bytes + 28);
    const std::size_t kept = log.events < kCardLogRecords ? log.events : kCardLogRecords;
    for (std::size_t i = 0; i < kept; ++i) {
        const std::uint8_t* r = bytes + 32 + i * 32;
        CardLogEvent e;
        e.kind = r[0];
        e.attempt = r[1];
        e.flags = (unsigned(r[2]) << 8) | r[3];
        e.result = static_cast<std::int32_t>(be32(r + 4));
        e.sector = be32(r + 8);
        e.count = be32(r + 12);
        e.status = be32(r + 16);
        e.time = be32(r + 20);
        e.since_write = be32(r + 24);
        e.polls = be32(r + 28);
        log.kept.push_back(e);
    }
    out = log;
    return true;
}

std::vector<std::string> describe_card_log(const CardLog& log) {
    static const char* const kPaths[] = {"/dev/sdio/slot0, SDSC", "/dev/sdio/slot0, SDHC", "d2x /dev/sdio/sdhc"};
    std::vector<std::string> lines;
    lines.push_back(format("SD card log of the last game: %u event(s) on %s; %u save transfers, %u failed, "
                           "%u sent again, %u status checks",
                           static_cast<unsigned>(log.events), log.path < 3 ? kPaths[log.path] : "an unknown path",
                           static_cast<unsigned>(log.transfers), static_cast<unsigned>(log.failures),
                           static_cast<unsigned>(log.retries), static_cast<unsigned>(log.settle_polls)));
    const std::uint32_t first = log.kept.empty() ? 0 : log.kept.front().time;
    for (const CardLogEvent& e : log.kept) {
        std::string line = format("  +%.3f s  ", (e.time - first) / kTicksPerSecond);
        if (e.kind == 7) {
            line += "mod file read failed";
        } else {
            line += format("save %s %s", (e.flags & 1u) ? "write" : "read", kind_name(e.kind));
            if (e.attempt != 0) line += format(" (try %u)", e.attempt);
        }
        line += format(": IPC %d, sector %u x%u, card status %s", static_cast<int>(e.result),
                       static_cast<unsigned>(e.sector), static_cast<unsigned>(e.count), status_text(e.status).c_str());
        if (e.polls != 0) line += format(", %u status checks", static_cast<unsigned>(e.polls));
        if (e.since_write == kCardLogNoWrite) {
            line += ", no save write yet";
        } else {
            line += format(", %.1f ms after the last save write", e.since_write * 1000.0 / kTicksPerSecond);
        }
        if (e.flags & 2u) line += ", from the IPC interrupt";
        if (e.flags & 4u) line += ", while a mod file was being read";
        lines.push_back(line);
    }
    if (log.events > log.kept.size()) {
        lines.push_back(format("  (%u later event(s) not kept)", static_cast<unsigned>(log.events - log.kept.size())));
    }
    return lines;
}

}  // namespace riftwii
