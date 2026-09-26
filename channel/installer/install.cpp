// SPDX-License-Identifier: GPL-3.0-or-later
#include "install.hpp"

#include <gccore.h>
#include <malloc.h>
#include <ogc/es.h>
#include <ogc/isfs.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <bearssl.h>

#include "riftwii_channel_bin.h"
#include "riftwii_channel_info.h"

namespace installer {
namespace {

// The package tools/make_channel.py writes: "RWCH", its format, the
// title ID, the channel's version, the part count, then per part its
// offset and size. The parts: the TMD, the ticket, then the contents:
// 0 the banner, 1 the boot program (channel/loader), 2 the forwarder;
// none encrypted or signed.
constexpr u32 kPackageMagic = 0x52574348;
constexpr u32 kPackageFormat = 1;
constexpr u32 kContents = 3;
constexpr u32 kParts = 2 + kContents;
constexpr u32 kTmdHeader = 0x1E4;
constexpr u32 kTmdContent = 0x24;
constexpr u32 kTicketSize = 0x2A4;
constexpr u32 kSignedFrom = 0x140;   // a signature covers the blob from its issuer on
constexpr u32 kTmdCounter = 0x1D4;   // TMD: last word of reserved2, free for fakesigning
constexpr u32 kTicketCounter = 0x21E;  // ticket: last word of the unused 0x30 bytes
constexpr u32 kTicketTitleKey = 0x1BF;
constexpr u32 kCommonKeyHandle = 4;  // IOS's handle for the console's common key
// The channel's own content key. It needs no secrecy: the ticket carries
// it encrypted with the console's common key, which only ES applies.
constexpr u8 kTitleKey[16] = {'R', 'i', 'f', 't', 'W', 'i', 'i', ' ', 'c', 'h', 'a', 'n', 'n', 'e', 'l', '1'};

u32 be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | p[3]; }
u64 be64(const u8* p) { return (u64(be32(p)) << 32) | be32(p + 4); }
void put32(u8* p, u32 v) {
    p[0] = u8(v >> 24);
    p[1] = u8(v >> 16);
    p[2] = u8(v >> 8);
    p[3] = u8(v);
}

struct Part {
    const u8* data = nullptr;
    u32 size = 0;
};

struct Package {
    u64 title = 0;
    u32 version = 0;
    Part parts[kParts];
};

// The package is built into this program (Makefile.channel).
bool parse(Package& out, std::string& error) {
    const u32 size = riftwii_channel_bin_size;
    const u8* p = riftwii_channel_bin;
    if (size < 0x20 + 8 * kParts || be32(p) != kPackageMagic || be32(p + 4) != kPackageFormat ||
        be32(p + 0x14) != kParts) {
        error = "the channel package in this installer is damaged";
        return false;
    }
    out.title = be64(p + 8);
    out.version = be32(p + 0x10);
    for (u32 i = 0; i < kParts; ++i) {
        const u32 at = be32(p + 0x18 + 8 * i), n = be32(p + 0x1C + 8 * i);
        if (at > size || n > size - at) {
            error = "the channel package in this installer is damaged";
            return false;
        }
        out.parts[i] = {p + at, n};
    }
    if (out.parts[0].size != kTmdHeader + kContents * kTmdContent || out.parts[1].size != kTicketSize ||
        out.title != RIFTWII_CHANNEL_TITLE || out.version != RIFTWII_CHANNEL_VERSION) {
        error = "the channel package in this installer is damaged";
        return false;
    }
    return true;
}

struct AlignedFree {
    void operator()(u8* p) const { std::free(p); }
};
using Buffer = std::unique_ptr<u8, AlignedFree>;

Buffer aligned_copy(const u8* data, u32 size) {
    Buffer b(static_cast<u8*>(memalign(32, (size + 31) & ~31u)));
    if (b) {
        std::memset(b.get(), 0, (size + 31) & ~31u);
        std::memcpy(b.get(), data, size);
    }
    return b;
}

// A signature of zeros and a hash that starts with a zero byte: what the
// signature patches of a d2x cIOS accept in place of Nintendo's.
void fakesign(u8* blob, u32 size, u32 counter_at) {
    std::memset(blob + 4, 0, 0x100);
    for (u32 n = 0;; ++n) {
        put32(blob + counter_at, n);
        br_sha1_context sha;
        br_sha1_init(&sha);
        br_sha1_update(&sha, blob + kSignedFrom, size - kSignedFrom);
        u8 hash[20];
        br_sha1_out(&sha, hash);
        if (hash[0] == 0) return;
    }
}

// The title key encrypted with the common key, done by ES (key handle 4)
// so the key itself never leaves IOS. Checked by decrypting it again.
bool encrypt_title_key(u64 title, u8 out[16], std::string& error) {
    static u32 iv[4] ATTRIBUTE_ALIGN(32);
    static u8 in[16] ATTRIBUTE_ALIGN(32);
    static u8 enc[16] ATTRIBUTE_ALIGN(32);
    static u8 back[16] ATTRIBUTE_ALIGN(32);
    const auto reset_iv = [&]() {
        iv[0] = u32(title >> 32);
        iv[1] = u32(title);
        iv[2] = iv[3] = 0;
    };
    std::memcpy(in, kTitleKey, 16);
    reset_iv();
    const s32 r = ES_Encrypt(kCommonKeyHandle, iv, in, 16, enc);
    if (r < 0) {
        error = "ES would not encrypt the channel's key (" + std::to_string(r) + ")";
        return false;
    }
    reset_iv();
    const s32 d = ES_Decrypt(kCommonKeyHandle, iv, enc, 16, back);
    if (d < 0 || std::memcmp(back, kTitleKey, 16) != 0) {
        error = "ES encrypted the channel's key but did not decrypt it back (" + std::to_string(d) + ")";
        return false;
    }
    std::memcpy(out, enc, 16);
    return true;
}

bool read_certs(Buffer& out, u32& size, std::string& error) {
    if (ISFS_Initialize() < 0) {
        error = "cannot open the Wii's NAND file system";
        return false;
    }
    const s32 fd = ISFS_Open("/sys/cert.sys", ISFS_OPEN_READ);
    if (fd < 0) {
        ISFS_Deinitialize();
        error = "cannot open /sys/cert.sys (" + std::to_string(fd) + ")";
        return false;
    }
    static fstats st ATTRIBUTE_ALIGN(32);
    s32 got = ISFS_GetFileStats(fd, &st);
    if (got >= 0 && st.file_length > 0 && st.file_length <= 0x4000) {
        out.reset(static_cast<u8*>(memalign(32, (st.file_length + 31) & ~31u)));
        got = out ? ISFS_Read(fd, out.get(), st.file_length) : -1;
    } else {
        got = -1;
    }
    ISFS_Close(fd);
    ISFS_Deinitialize();
    if (got <= 0 || static_cast<u32>(got) != st.file_length) {
        error = "cannot read /sys/cert.sys";
        return false;
    }
    size = st.file_length;
    return true;
}

// One content, encrypted as ES wants it (AES-128-CBC with the title key,
// the content index as the IV, zero padding to 16 bytes), fed in chunks.
bool add_content(u64 title, u32 cid, u16 index, const Part& part, std::string& error) {
    const s32 cfd = ES_AddContentStart(title, cid);
    if (cfd < 0) {
        error = "ES_AddContentStart " + std::to_string(cid) + ": " + std::to_string(cfd);
        return false;
    }
    br_aes_ct_cbcenc_keys aes;
    br_aes_ct_cbcenc_init(&aes, kTitleKey, sizeof(kTitleKey));
    u8 iv[16] = {u8(index >> 8), u8(index)};
    static u8 chunk[0x8000] ATTRIBUTE_ALIGN(32);
    const u32 padded = (part.size + 15) & ~15u;
    for (u32 at = 0; at < padded; at += sizeof(chunk)) {
        const u32 n = std::min<u32>(sizeof(chunk), padded - at);
        std::memset(chunk, 0, n);
        if (at < part.size) std::memcpy(chunk, part.data + at, std::min(n, part.size - at));
        br_aes_ct_cbcenc_run(&aes, iv, chunk, n);
        const s32 r = ES_AddContentData(cfd, chunk, n);
        if (r < 0) {
            error = "ES_AddContentData " + std::to_string(cid) + ": " + std::to_string(r);
            return false;
        }
    }
    const s32 r = ES_AddContentFinish(cfd);
    if (r < 0) {
        error = "ES_AddContentFinish " + std::to_string(cid) + ": " + std::to_string(r);
        return false;
    }
    return true;
}

bool delete_tickets(u64 title, std::string& error) {
    u32 count = 0;
    if (ES_GetNumTicketViews(title, &count) < 0 || count == 0) return true;
    std::unique_ptr<tikview, void (*)(void*)> views(static_cast<tikview*>(memalign(32, sizeof(tikview) * count)), std::free);
    if (!views || ES_GetTicketViews(title, views.get(), count) < 0) {
        error = "cannot read the channel's tickets";
        return false;
    }
    for (u32 i = 0; i < count; ++i) {
        const s32 r = ES_DeleteTicket(&views.get()[i]);
        if (r < 0) {
            error = "ES_DeleteTicket: " + std::to_string(r);
            return false;
        }
    }
    return true;
}

}  // namespace

unsigned PackageVersion() { return RIFTWII_CHANNEL_VERSION; }

bool Installed(unsigned& version) {
    version = 0;
    const u64 title = RIFTWII_CHANNEL_TITLE;
    u32 tickets = 0;
    if (ES_GetNumTicketViews(title, &tickets) < 0 || tickets == 0) return false;
    u32 size = 0;
    if (ES_GetTMDViewSize(title, &size) < 0 || size < sizeof(tmd_view) || size > 0x400) return false;
    static u8 view[0x400] ATTRIBUTE_ALIGN(32);
    if (ES_GetTMDView(title, reinterpret_cast<tmd_view*>(view), size) < 0) return false;
    version = reinterpret_cast<const tmd_view*>(view)->title_version;
    return true;
}

bool Install(std::string& error) {
    Package pkg;
    Log("Installing the channel, version %u\n", PackageVersion());
    bool ok = parse(pkg, error);
    Buffer certs;
    u32 certs_size = 0;
    u8 enc_key[16];
    ok = ok && read_certs(certs, certs_size, error) && encrypt_title_key(pkg.title, enc_key, error);
    Buffer tmd, ticket;
    if (ok) {
        tmd = aligned_copy(pkg.parts[0].data, pkg.parts[0].size);
        ticket = aligned_copy(pkg.parts[1].data, pkg.parts[1].size);
        ok = tmd && ticket;
        if (!ok) error = "out of memory";
    }
    if (ok) {
        std::memcpy(ticket.get() + kTicketTitleKey, enc_key, 16);
        fakesign(ticket.get(), kTicketSize, kTicketCounter);
        fakesign(tmd.get(), pkg.parts[0].size, kTmdCounter);
        const s32 r = ES_AddTicket(reinterpret_cast<signed_blob*>(ticket.get()), kTicketSize,
                                   reinterpret_cast<signed_blob*>(certs.get()), certs_size, nullptr, 0);
        if (r < 0) {
            error = "ES_AddTicket: " + std::to_string(r);
            ok = false;
        }
    }
    if (ok) {
        const s32 r = ES_AddTitleStart(reinterpret_cast<signed_blob*>(tmd.get()), pkg.parts[0].size,
                                       reinterpret_cast<signed_blob*>(certs.get()), certs_size, nullptr, 0);
        if (r < 0) {
            error = "ES_AddTitleStart: " + std::to_string(r);
            ok = false;
        } else {
            for (u32 i = 0; ok && i < kContents; ++i) ok = add_content(pkg.title, i, u16(i), pkg.parts[2 + i], error);
            const s32 f = ok ? ES_AddTitleFinish() : ES_AddTitleCancel();
            if (ok && f < 0) {
                error = "ES_AddTitleFinish: " + std::to_string(f);
                ok = false;
            }
        }
    }
    if (ok) Log("Installed (IOS%d)\n", IOS_GetVersion());
    else Log("Install failed: %s\n", error.c_str());
    return ok;
}

bool Remove(std::string& error) {
    const u64 title = RIFTWII_CHANNEL_TITLE;
    Log("Removing the channel\n");
    bool ok = true;
    const s32 r = ES_DeleteTitle(title);
    if (r < 0 && r != -106) {  // -106: not there
        error = "ES_DeleteTitle: " + std::to_string(r);
        ok = false;
    }
    ok = ok && delete_tickets(title, error);
    if (ok) Log("Removed\n");
    else Log("Removal failed: %s\n", error.c_str());
    return ok;
}

}  // namespace installer
