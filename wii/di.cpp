// SPDX-License-Identifier: GPL-3.0-or-later
#include "di.hpp"

#include <gccore.h>
#include <ogc/cache.h>
#include <ogc/ipc.h>

#include <algorithm>
#include <cstring>

namespace riftwii::di {
namespace {

constexpr std::uint32_t kInquiry = 0x12;
constexpr std::uint32_t kReadDiscId = 0x70;
constexpr std::uint32_t kRead = 0x71;
constexpr std::uint32_t kWaitForCoverClose = 0x79;
constexpr std::uint32_t kGetCoverStatus = 0x88;
constexpr std::uint32_t kReset = 0x8A;
constexpr std::uint32_t kOpenPartition = 0x8B;
constexpr std::uint32_t kClosePartition = 0x8C;
constexpr std::uint32_t kReadUnencrypted = 0x8D;
constexpr std::uint32_t kResetDisable = 0xF6;
constexpr std::uint32_t kFragSet = 0xF9;
constexpr std::uint32_t kModeGet = 0xFA;

char g_path[] ATTRIBUTE_ALIGN(32) = "/dev/di";
std::uint32_t g_in[8] ATTRIBUTE_ALIGN(32);
std::uint32_t g_out[8] ATTRIBUTE_ALIGN(32);
ioctlv g_vectors[5] ATTRIBUTE_ALIGN(32);
std::uint8_t g_bounce[32 * 1024] ATTRIBUTE_ALIGN(32);
s32 g_fd = -1;
int g_last_reply = 0;
std::uint32_t g_frag_device = 0;

bool aligned32(const void* p) { return (reinterpret_cast<std::uintptr_t>(p) & 31) == 0; }

std::string describe(const char* what, int reply) {
    return std::string(what) + " failed: DI reply " + std::to_string(reply);
}

// Runs a plain ioctl; `expected` is the reply that means success.
bool command(std::uint32_t cmd, const char* what, void* out, std::uint32_t out_len, int expected,
             std::string& error) {
    if (g_fd < 0) {
        error = std::string(what) + ": /dev/di is not open";
        return false;
    }
    g_in[0] = cmd << 24;
    g_last_reply = IOS_Ioctl(g_fd, static_cast<s32>(cmd), g_in, sizeof(g_in), out, out_len);
    if (g_last_reply != expected) {
        error = describe(what, g_last_reply);
        return false;
    }
    error.clear();
    return true;
}

bool read_command(std::uint32_t cmd, const char* what, void* buffer, std::uint32_t length,
                  std::uint32_t word_offset, std::string& error) {
    if (!aligned32(buffer) || (length & 31) != 0) {
        error = std::string(what) + ": buffer must be 32-byte aligned and a multiple of 32 bytes";
        return false;
    }
    std::memset(g_in, 0, sizeof(g_in));
    g_in[1] = length;
    g_in[2] = word_offset;
    return command(cmd, what, buffer, length, kReplySuccess, error);
}

// Bounces an arbitrary byte range through aligned DI reads.
bool bounced_read(bool partition, std::uint64_t offset, std::uint8_t* destination, std::size_t length,
                  std::string& error) {
    while (length > 0) {
        const std::uint64_t start = offset & ~std::uint64_t(31);
        std::uint64_t end = (offset + length + 31) & ~std::uint64_t(31);
        if (end - start > sizeof(g_bounce)) end = start + sizeof(g_bounce);
        if (end > (std::uint64_t(1) << 34)) {  // the last word must fit the drive's 32-bit offset
            error = "disc offset beyond the drive's 32-bit word range";
            return false;
        }
        const std::uint32_t chunk = static_cast<std::uint32_t>(end - start);
        const bool ok = partition ? read(g_bounce, chunk, static_cast<std::uint32_t>(start >> 2), error)
                                  : read_unencrypted(g_bounce, chunk, static_cast<std::uint32_t>(start >> 2), error);
        if (!ok) return false;
        const std::size_t skip = static_cast<std::size_t>(offset - start);
        const std::size_t take = std::min<std::size_t>(length, static_cast<std::size_t>(end - offset));
        std::memcpy(destination, g_bounce + skip, take);
        offset += take;
        destination += take;
        length -= take;
    }
    return true;
}

}  // namespace

bool open(std::string& error) {
    if (g_fd >= 0) {
        error.clear();
        return true;
    }
    const s32 fd = IOS_Open(g_path, 0);
    if (fd < 0) {
        g_last_reply = fd;
        error = "cannot open /dev/di: IOS error " + std::to_string(fd);
        return false;
    }
    g_fd = fd;
    error.clear();
    return true;
}

void close() {
    if (g_fd >= 0) IOS_Close(g_fd);
    g_fd = -1;
}

bool is_open() { return g_fd >= 0; }

int last_reply() { return g_last_reply; }

bool cover_status(bool& disc_inserted, std::string& error) {
    std::memset(g_in, 0, sizeof(g_in));
    std::memset(g_out, 0, sizeof(g_out));
    if (!command(kGetCoverStatus, "get cover status", g_out, sizeof(g_out), kReplySuccess, error)) return false;
    disc_inserted = (g_out[0] & 2) != 0;
    return true;
}

bool wait_for_cover_close(std::string& error) {
    std::memset(g_in, 0, sizeof(g_in));
    return command(kWaitForCoverClose, "wait for cover close", nullptr, 0, kReplyCoverClosed, error);
}

bool reset(bool spin_up, std::string& error) {
    std::memset(g_in, 0, sizeof(g_in));
    g_in[1] = spin_up ? 1 : 0;
    return command(kReset, "drive reset", nullptr, 0, kReplySuccess, error);
}

bool probe_d2x(std::uint32_t& mode, std::string& error) {
    if (g_fd < 0) { error = "d2x mode probe: /dev/di is not open"; return false; }
    std::memset(g_in, 0, sizeof(g_in)); std::memset(g_out, 0, sizeof(g_out));
    g_in[0] = kModeGet << 24;
    g_last_reply = IOS_Ioctl(g_fd, static_cast<s32>(kModeGet), g_in, sizeof(g_in), g_out, sizeof(g_out));
    if (g_last_reply < 0) { error = describe("d2x mode probe", g_last_reply); return false; }
    mode = g_out[0]; error.clear(); return true;
}

bool disable_reset(std::string& error) {
    if (g_fd < 0) { error = "d2x reset-disable: /dev/di is not open"; return false; }
    std::memset(g_in, 0, sizeof(g_in)); std::memset(g_out, 0, sizeof(g_out));
    g_in[0] = kResetDisable << 24;
    g_in[1] = 1;  // d2x plugin.c assigns config.noreset = inbuf[1].
    g_last_reply = IOS_Ioctl(g_fd, static_cast<s32>(kResetDisable), g_in, sizeof(g_in), g_out, sizeof(g_out));
    if (g_last_reply < 0 || g_out[0] != 0) {
        error = g_last_reply < 0 ? describe("d2x reset-disable", g_last_reply) : "d2x reset-disable rejected the request: " + std::to_string(g_out[0]);
        return false;
    }
    error.clear(); return true;
}

bool configure_frag(std::uint32_t device, const void* list32, std::uint32_t bytes, std::string& error) {
    if (g_fd < 0) { error = "d2x fragment setup: /dev/di is not open"; return false; }
    if (!list32 || !aligned32(list32) || bytes < 12 || (bytes & 3) != 0) {
        error = "d2x fragment setup needs a 32-byte-aligned nonempty fragment list"; return false;
    }
    DCFlushRange(const_cast<void*>(list32), bytes);
    std::memset(g_in, 0, sizeof(g_in)); std::memset(g_out, 0, sizeof(g_out));
    if (device != 1 && device != 2) { error = "d2x fragment device must be USB (1) or SDHC (2)"; return false; }
    g_in[0] = kFragSet << 24; g_in[1] = device; // DEV_USB=1, DEV_SDHC=2
    // d2x converts inbuf[2] with VirtToPhys (clear bit 31). Passing the
    // physical form required by F9 is therefore idempotent on the IOS side.
    g_in[2] = MEM_VIRTUAL_TO_PHYSICAL(list32); g_in[3] = bytes;
    DCFlushRange(g_in, sizeof(g_in)); DCInvalidateRange(g_out, sizeof(g_out));
    g_last_reply = IOS_Ioctl(g_fd, static_cast<s32>(kFragSet), g_in, sizeof(g_in), g_out, sizeof(g_out));
    DCInvalidateRange(g_out, sizeof(g_out));
    if (g_last_reply < 0 || g_out[0] != 0) {
        error = g_last_reply < 0 ? describe("d2x fragment setup", g_last_reply) : "d2x rejected the fragment list: " + std::to_string(g_out[0]);
        return false;
    }
    g_frag_device = device;
    error.clear(); return true;
}

std::uint32_t frag_device() { return g_frag_device; }

bool inquiry(std::uint8_t out32[32], std::string& error) {
    std::memset(g_in, 0, sizeof(g_in));
    std::memset(g_out, 0, sizeof(g_out));
    if (!command(kInquiry, "inquiry", g_out, sizeof(g_out), kReplySuccess, error)) return false;
    std::memcpy(out32, g_out, 32);
    return true;
}

bool read_disc_id(std::uint8_t out32[32], std::string& error) {
    std::memset(g_in, 0, sizeof(g_in));
    std::memset(g_out, 0, sizeof(g_out));
    if (!command(kReadDiscId, "read disc id", g_out, sizeof(g_out), kReplySuccess, error)) return false;
    std::memcpy(out32, g_out, 32);
    return true;
}

bool read_unencrypted(void* buffer32, std::uint32_t length, std::uint32_t word_offset, std::string& error) {
    return read_command(kReadUnencrypted, "unencrypted read", buffer32, length, word_offset, error);
}

namespace {
PartitionResolver g_resolver;
const ByteSource* g_partition_data = nullptr;
}

void set_partition_resolver(PartitionResolver resolve) {
    g_resolver = std::move(resolve);
    g_partition_data = nullptr;
}

bool has_partition_resolver() { return static_cast<bool>(g_resolver); }

bool open_partition(std::uint32_t word_offset, std::uint8_t* tmd_out32, std::size_t tmd_capacity,
                    std::int32_t& es_result, std::string& error) {
    if (g_fd < 0) {
        error = "open partition: /dev/di is not open";
        return false;
    }
    if (!aligned32(tmd_out32) || tmd_capacity < kTmdBufferBytes) {
        error = "open partition: TMD buffer must be 32-byte aligned and hold 0x49E4 bytes";
        return false;
    }
    std::memset(g_in, 0, sizeof(g_in));
    std::memset(g_out, 0, sizeof(g_out));
    g_in[0] = kOpenPartition << 24;
    g_in[1] = word_offset;
    // in: parameters, ticket (none), certificate chain (none); out: TMD, ES result.
    g_vectors[0].data = g_in;
    g_vectors[0].len = sizeof(g_in);
    g_vectors[1].data = nullptr;
    g_vectors[1].len = 0;
    g_vectors[2].data = nullptr;
    g_vectors[2].len = 0;
    g_vectors[3].data = tmd_out32;
    g_vectors[3].len = kTmdBufferBytes;
    g_vectors[4].data = g_out;
    g_vectors[4].len = sizeof(g_out);
    g_last_reply = IOS_Ioctlv(g_fd, static_cast<s32>(kOpenPartition), 3, 2, g_vectors);
    es_result = static_cast<std::int32_t>(g_out[0]);
    if (g_last_reply != kReplySuccess) {
        error = describe("open partition", g_last_reply) + " (ES " + std::to_string(es_result) + ")";
        return false;
    }
    g_partition_data = g_resolver ? g_resolver(std::uint64_t(word_offset) << 2) : nullptr;
    error.clear();
    return true;
}

bool close_partition(std::string& error) {
    g_partition_data = nullptr;
    std::memset(g_in, 0, sizeof(g_in));
    return command(kClosePartition, "close partition", nullptr, 0, kReplySuccess, error);
}

bool read(void* buffer32, std::uint32_t length, std::uint32_t word_offset, std::string& error) {
    if (g_partition_data != nullptr) {
        if (!g_partition_data->read(std::uint64_t(word_offset) << 2, static_cast<std::uint8_t*>(buffer32), length)) {
            error = "partition read of " + std::to_string(length) + " bytes at word " + std::to_string(word_offset) +
                    " from the RVZ failed";
            return false;
        }
        error.clear();
        return true;
    }
    return read_command(kRead, "partition read", buffer32, length, word_offset, error);
}

bool SystemAreaSource::read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const {
    if (offset > kReadableBytes || length > kReadableBytes - offset) return false;
    std::string error;
    return bounced_read(false, offset, destination, length, error);
}

bool PartitionSource::read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const {
    if (offset > bytes_ || length > bytes_ - offset) return false;
    std::string error;
    return bounced_read(true, offset, destination, length, error);
}

}  // namespace riftwii::di
