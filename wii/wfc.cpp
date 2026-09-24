// SPDX-License-Identifier: GPL-3.0-or-later
#include "wfc.hpp"

#include <gccore.h>
#include <ogc/cache.h>

#include <cstdio>
#include <cstring>

#include "log.hpp"
#include "wwfcGameAddresses.hpp"
#include "wwfcStage1Payload.hpp"

extern "C" {
extern u32 g_wwfcPatchStart[];
extern u32 g_wwfcPatchLoadAuthWorkReq[];
extern u32 g_wwfcPatchAuthExit[];
extern u32 g_wwfcPatchStage1Data[];
extern u32 g_wwfcPatchEnd[];
}

namespace riftwii::wii {
namespace {

u32 Read32(u32 address) { return *reinterpret_cast<volatile u32*>(address); }
void Write32(u32 address, u32 value) { *reinterpret_cast<volatile u32*>(address) = value; }
void Flush(u32 address, u32 length) {
    DCFlushRange(reinterpret_cast<void*>(address & ~31u), (length + (address & 31u) + 31u) & ~31u);
    ICInvalidateRange(reinterpret_cast<void*>(address & ~31u), (length + (address & 31u) + 31u) & ~31u);
}
u32 Branch(u32 from, u32 to) { return 0x48000000u | ((to - from) & 0x03FFFFFCu); }

// The MEM1 arena end moves down to `new_end`, where both globals say it.
void LowerArena1(u32 new_end) {
    if (Read32(0x80000034) != 0) Write32(0x80000034, new_end);
    Write32(0x80003110, new_end);
    Flush(0x80000034, 4);
    Flush(0x80003110, 4);
}

// Mario Kart Wii's remote code execution fix: seven 0xFF words over the
// hole, unless an up-to-date patcher already fixed the image.
void MkwRceFix(char region) {
    u32 patched = 0, at = 0;
    switch (region) {
        case 'P': patched = 0x80276054; at = 0x8089a194; break;
        case 'E': patched = 0x80271d14; at = 0x80895ac4; break;
        case 'J': patched = 0x802759f4; at = 0x808992f4; break;
        case 'K': patched = 0x80263E34; at = 0x808885cc; break;
        default: return;
    }
    if (*reinterpret_cast<volatile char*>(patched) != '*') {
        logf("WFC: this Mario Kart Wii is already patched; no RCE fix needed\n");
        return;
    }
    for (u32 i = 0; i < 7; ++i) Write32(at + i * 4, 0xFF);
    Flush(at, 7 * 4);
    logf("WFC: Mario Kart Wii RCE fix applied\n");
}

// Wiimmfi's Mario Kart Wii patch (Leseratte, 2018; the error 51420 fix,
// 2021), as USB Loader GX applies it. The blob is Wiimmfi's; do not edit.
bool MkwWiimmfi(char region) {
    u32 patched, patch1, patch2, patch3, errorfix, urls, https_at, retry_at;
    const char* url3;
    switch (region) {
        case 'P':
            patched = 0x80276054; patch1 = 0x800ee3a0; patch2 = 0x801d4efc; patch3 = 0x801A72E0; errorfix = 0x80658ce4;
            urls = 0x8027A400; https_at = 0x802a146c; retry_at = 0x800ecaac; url3 = "https://main.nas.wiimmfi.de/pp";
            break;
        case 'E':
            patched = 0x80271d14; patch1 = 0x800ee300; patch2 = 0x801d4e5c; patch3 = 0x801A7240; errorfix = 0x8065485c;
            urls = 0x802760C0; https_at = 0x8029D12C; retry_at = 0x800ECA0C; url3 = "https://main.nas.wiimmfi.de/pe";
            break;
        case 'J':
            patched = 0x802759f4; patch1 = 0x800ee2c0; patch2 = 0x801d4e1c; patch3 = 0x801A7200; errorfix = 0x80658360;
            urls = 0x80279DA0; https_at = 0x802A0E0C; retry_at = 0x800EC9CC; url3 = "https://main.nas.wiimmfi.de/pj";
            break;
        case 'K':
            patched = 0x80263E34; patch1 = 0x800ee418; patch2 = 0x801d5258; patch3 = 0x801A763c; errorfix = 0x80646ffc;
            urls = 0x802682B0; https_at = 0x8028F474; retry_at = 0x800ECB24; url3 = "https://main.nas.wiimmfi.de/pk";
            break;
        default:
            return false;
    }
    char* mark = reinterpret_cast<char*>(patched);
    if (*mark != '*') {
        logf("WFC: this Mario Kart Wii is already Wiimmfi-patched\n");
        return true;
    }
    // Wiimmfi asks patchers to name themselves here.
    char name[42] = {};
    std::snprintf(name, sizeof(name), "RiftWii %-33s", RIFTWII_VERSION);
    std::memcpy(mark, name, sizeof(name));  // zero-padded, as strncpy would

    std::uint8_t* code = reinterpret_cast<std::uint8_t*>(0x80004000);
    patch_https_to_http(code, 0x385200);
    patch_wfc_domain(code, 0x385200, "wiimmfi.de");
    Flush(0x80004000, 0x385200);

    static const char kUrl1[] = "http://ca.nas.wiimmfi.de/ca";
    static const char kUrl2[] = "http://naswii.wiimmfi.de/ac";
    std::memcpy(reinterpret_cast<void*>(urls), kUrl1, sizeof(kUrl1));
    std::memcpy(reinterpret_cast<void*>(urls + 0x28), kUrl2, sizeof(kUrl2));
    std::memcpy(reinterpret_cast<void*>(urls + 0x4C), url3, std::strlen(url3) + 1);
    Flush(urls, 0x80);
    Write32(https_at, 0x733a2f2f);
    Write32(retry_at, 0x3bc00000);
    Flush(https_at, 4);
    Flush(retry_at, 4);

    const u32 old_end = Read32(0x80003110);
    const u32 heap = old_end - 0x500;
    LowerArena1(heap);
    std::memset(reinterpret_cast<void*>(heap), 0xed, 0x500);

    u32 binary[] = {
        0x37C849A2, 0x8BC32FA4, 0xC9A34B71, 0x1BCB49A2, 0x2F119304, 0x5F402684, 0x3E4FDA29, 0x50849A21,
        0xB88B3452, 0x627FC9C1, 0xDC24D119, 0x5844350F, 0xD893444F, 0x19A588DC, 0x16C91184, 0x0C3E237C,
        0x75906CED, 0x6E68A55E, 0x58791842, 0x072237E9, 0xAB24906F, 0x0A8BDF21, 0x4D11BE42, 0x1AAEDDC8,
        0x1C42F908, 0x280CF2B2, 0x453A1BA4, 0x9A56C869, 0x786F108E, 0xE8DF05D2, 0x6DB641EB, 0x6DFC84BB,
        0x7E980914, 0x0D7FB324, 0x23442185, 0xA7744966, 0x53901359, 0xBF2103CC, 0xC24A4EB7, 0x32049A02,
        0xC1683466, 0xCA93689D, 0xD8245106, 0xA84987CF, 0xEC9B47C9, 0x6FA688FE, 0x0A4D11A6, 0x8B653C7B,
        0x09D27E30, 0x5B936208, 0x5DD336DE, 0xCD092487, 0xEF2C6D36, 0x1E09DF2D, 0x75B1BE47, 0xE68A7F22,
        0xB0E5F90D, 0xEC49F216, 0xAD1DCC24, 0xE2B5C841, 0x066F6F63, 0xF4D90926, 0x299F42CD, 0xA3F125D6,
        0x077B093C, 0xB5721268, 0x1BE424D1, 0xEBC30BF0, 0x77867BED, 0x4F0C9BCA, 0x3E195930, 0xDC32DE2C,
        0x1865D189, 0x70C67E7A, 0x71FA7329, 0x532233D3, 0x06D2E87B, 0x6CBEBA7F, 0x99F08532, 0x52FA601C,
        0x05F4B82C, 0x4B64839C, 0xB5C65009, 0x1B8396E3, 0x0A8B2DAF, 0x0DB85BE6, 0x12F1B71D, 0x186F6E4D,
        0x2870DC2E, 0x5960B8E6, 0x8F4D71BD, 0x0614E3C3, 0x05E8C725, 0x365D8E3D, 0x74351CDE, 0xE1AB3930,
        0xFEDA721B, 0xE53AE4E9, 0xC3B4C9A6, 0xBAE59346, 0x6D45269D, 0x634E4D1A, 0x2FD99A30, 0x26393449,
        0xE49768D1, 0x81E1D1A1, 0xFCE1A34A, 0x7EB44697, 0xEB2F8D2D, 0xCECFE5AF, 0x81BD34B6, 0xB1F1696E,
        0x5E6ED2B2, 0xA473A4A0, 0x41664B70, 0xBF40968A, 0x662F2CCB, 0xC5DF5B8C, 0xB632B772, 0x74EB6F39,
        0xE017DC71, 0xFDA3B890, 0xE3C9713D, 0xCE53E397, 0xA12BC743, 0x5AD98EA5, 0xBC721C9F, 0x4568395A,
        0x925E72B4, 0x2D7DE4D7, 0x6777C9C7, 0xD6619396, 0xA502268A, 0x77884D75, 0xF79E9AF0, 0xE6FC3461,
        0xF07468A5, 0xF866D11D, 0xF90CA342, 0xCF9546FF, 0x87A48D81, 0x06881A51, 0x309C34D1, 0x79B669CE,
        0xFAADD2D7, 0xC8D7A5D1, 0x89214BE5, 0x1B8396EF, 0x0A8B2DE9, 0x0D985B06, 0x12F1B711, 0x186F6E57,
        0x2850DC0E, 0x5960B8EA, 0x8F4D71AC, 0x0614E3E3, 0x05E8C729, 0x365D8E39, 0x74351CFE, 0x518E3943,
        0x4A397268, 0x9D58E4B8, 0xD394C9A2, 0x0E069344, 0xB522268B, 0x636E4D77, 0x2FF99A37, 0xF6DC346D,
        0xE49268B4, 0x2001D1A0, 0x4929A365, 0x7B764691, 0xFFC68D49, 0x16A81A53, 0x247A34D2, 0xA1D16967,
        0x4B6DD2D5, 0xDDF4A5B7, 0x454A4B70, 0x0FAE96E2, 0x0A8A2DC7, 0x0D98A47A, 0x06DCB71D, 0x0CCC6E38,
        0x55F25CFB, 0xB08C1E88, 0xDF4259C9, 0x0714E387, 0xB00D47AF, 0x7B722975, 0x48BE349A, 0x29CC393C,
        0xEA797228, 0x98986471, 0x3778E1A3, 0xD7626D06, 0x1567268D, 0x668ECD00, 0xD614F5C8, 0x133037CF,
        0x92F26CF2, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000};
    u32 fix51420[] = {
        0x4800000d, 0x00000000, 0x00000000, 0x7cc803a6, 0x80860000, 0x7c041800, 0x4182004c, 0x80a60004,
        0x38a50001, 0x2c050003, 0x4182003c, 0x90a60004, 0x90660000, 0x38610010, 0x3ca08066, 0x38a58418,
        0x3c808066, 0x38848498, 0x90a10010, 0x90810014, 0x3ce08066, 0x38e78ce4, 0x38e7fef0, 0x7ce903a6,
        0x4e800420, 0x3c80801d, 0x388415f4, 0x7c8803a6, 0x4e800021, 0x00000000};
    // Unpack the blob.
    for (int i = 3, idx = 0; i < 202; ++i) {
        if (i == 67 || i == 82) ++idx;
        binary[i] ^= binary[idx];
        binary[idx] = (binary[idx] << 1) | (binary[idx] >> 31);
    }
    switch (region) {
        case 'E':
            binary[29] = binary[67]; binary[37] = binary[68]; binary[43] = binary[69];
            binary[185] = 0x61295C74; binary[189] = 0x61295D40; binary[198] = 0x61086F5C;
            fix51420[14] = 0x3ca08065; fix51420[15] = 0x38a53f90; fix51420[16] = 0x3c808065;
            fix51420[17] = 0x38844010; fix51420[20] = 0x3ce08065; fix51420[21] = 0x38e7485c; fix51420[26] = 0x38841554;
            break;
        case 'J':
            binary[29] = binary[70]; binary[37] = binary[71]; binary[43] = binary[72];
            binary[185] = 0x612997CC; binary[189] = 0x61299898; binary[198] = 0x61086F1C;
            fix51420[14] = 0x3ca08065; fix51420[15] = 0x38a57a84; fix51420[16] = 0x3c808065;
            fix51420[17] = 0x38847b04; fix51420[20] = 0x3ce08065; fix51420[21] = 0x38e78350; fix51420[26] = 0x38841514;
            break;
        case 'K':
            binary[6] = binary[73]; binary[9] = binary[74]; binary[11] = binary[75]; binary[23] = binary[76];
            binary[29] = binary[77]; binary[33] = binary[78]; binary[37] = binary[79]; binary[43] = binary[80];
            binary[63] = binary[81]; binary[184] = 0x3D208088; binary[185] = 0x61298AA4; binary[188] = 0x3D208088;
            binary[189] = 0x61298B58; binary[198] = 0x61087358;
            fix51420[14] = 0x3ca08064; fix51420[15] = 0x38a56730; fix51420[16] = 0x3c808064;
            fix51420[17] = 0x388467b0; fix51420[20] = 0x3ce08064; fix51420[21] = 0x38e76ffc; fix51420[26] = 0x38841950;
            break;
        default:
            break;
    }
    std::memcpy(reinterpret_cast<void*>(heap), binary, 820);
    Write32(patch1, Branch(patch1, heap + 12));
    Write32(heap + 88, Branch(heap + 88, patch1 + 4));
    Write32(patch2, Branch(patch2, heap + 92));
    Write32(heap + 264, Branch(heap + 264, patch2 + 4));
    Write32(patch3, Branch(patch3, heap + 328));
    std::memcpy(reinterpret_cast<void*>(heap + 0x400), fix51420, sizeof(fix51420));
    Write32(errorfix, Branch(errorfix, heap + 0x400));
    // The fix's last word returns after the patched instruction. (USB
    // Loader GX computes this address with u32 pointer arithmetic, which
    // lands 0x11D0 bytes past the block instead.)
    Write32(heap + 0x400 + 0x74, Branch(heap + 0x400 + 0x74, errorfix + 4));
    Flush(heap, 0x500);
    Flush(patch1, 4);
    Flush(patch2, 4);
    Flush(patch3, 4);
    Flush(errorfix, 4);
    logf("WFC: Wiimmfi's Mario Kart Wii patch installed at %08X\n", static_cast<unsigned>(heap));
    return true;
}

// WiiLink WFC, as their launcher's PatchAndLaunchDol does it.
struct Stage1Param {
    u32 p_block;
    u32 p_NHTTPCreateRequest;
    u32 p_NHTTPSendRequestAsync;
    u32 p_NHTTPDestroyResponse;
    u32 p_allocator;
    u32 p_dwcError;
    char title[9];
    u8 finished;
    u8 padding[2];
    u32 p_stage1;
};
static_assert(sizeof(Stage1Param) == 0x28, "matches g_wwfcPatchStage1Data in wwfcPatch.S");

bool WiiLink(const std::string& game_id, std::uint8_t disc_version) {
    char title[16];
    std::snprintf(title, sizeof(title), "%.4sD%02x", game_id.c_str(), disc_version);
    const GameAddresses* game = nullptr;
    for (u32 i = 0; i < GameAddressesListSize; ++i) {
        if (std::strcmp(title, GameAddressesList[i].gameId) == 0) {
            game = &GameAddressesList[i];
            break;
        }
    }
    if (!game) {
        logf("WFC: %s is not in WiiLink WFC's list of games; not patched\n", title);
        return false;
    }
    // A block at the top of MEM2 for what the downloader fetches.
    const u32 mem2_end = Read32(0x80003128);
    const u32 block = (mem2_end - 0x20000) & ~31u;
    Write32(0x80003128, block);
    Flush(0x80003128, 4);

    const u32 stage1 = (Read32(0x80003110) - sizeof(Stage1Payload)) & ~31u;
    std::memcpy(reinterpret_cast<void*>(stage1), Stage1Payload, sizeof(Stage1Payload));
    Flush(stage1, sizeof(Stage1Payload));

    // The hook's code, copied then filled in.
    const u32 start = reinterpret_cast<u32>(g_wwfcPatchStart);
    const u32 length = reinterpret_cast<u32>(g_wwfcPatchEnd) - start;
    const u32 hook = stage1 - 0x100;
    if (length > 0x100) return false;
    std::memcpy(reinterpret_cast<void*>(hook), g_wwfcPatchStart, length);
    const auto at = [&](const void* symbol) { return hook + (reinterpret_cast<u32>(symbol) - start); };
    const u32 data = at(g_wwfcPatchStage1Data);
    Write32(hook + 0, (Read32(hook + 0) & ~0xFFFFu) | (data >> 16));
    Write32(hook + 4, (Read32(hook + 4) & ~0xFFFFu) | (data & 0xFFFF));
    const u32 load = at(g_wwfcPatchLoadAuthWorkReq);
    for (u32 i = 0; i < 3; ++i) Write32(load + i * 4, game->loadAuthRequestAsm[i]);
    const u32 exit = at(g_wwfcPatchAuthExit);
    Write32(exit, Read32(game->addrAuthSendRequest));
    Write32(exit + 4, Branch(exit + 4, game->addrAuthSendRequest + 4));
    Stage1Param param = {};
    param.p_block = block;
    param.p_NHTTPCreateRequest = game->addrNHTTPCreateRequest;
    param.p_NHTTPSendRequestAsync = game->addrNHTTPSendRequest;
    param.p_NHTTPDestroyResponse = game->addrNHTTPDestroyResponse;
    param.p_dwcError = game->addrNASError;
    std::snprintf(param.title, sizeof(param.title), "%s", game->gameId);
    param.p_stage1 = stage1;
    std::memcpy(reinterpret_cast<void*>(data), &param, sizeof(param));
    Flush(hook, 0x100);

    // Into the game: the hook, no DNS cache at login, WiiLink's
    // availability check domain.
    Write32(game->addrAuthSendRequest, Branch(game->addrAuthSendRequest, hook));
    Flush(game->addrAuthSendRequest, 4);
    Write32(game->addrSkipDNSCache, Branch(game->addrSkipDNSCache, game->addrSkipDNSCacheContinue));
    Flush(game->addrSkipDNSCache, 4);
    std::strcpy(reinterpret_cast<char*>(game->addrAvailableDomain), "%s.av.gs.wiilink.ca");
    Flush(game->addrAvailableDomain, 32);

    LowerArena1(hook);
    logf("WFC: WiiLink WFC hook for %s; downloader at %08X, block at %08X\n", title, static_cast<unsigned>(stage1),
         static_cast<unsigned>(block));
    return true;
}

}  // namespace

void ApplyWfc(const std::vector<MemoryRegion>& loaded, WfcServer server, const std::string& domain,
              const std::string& game_id, std::uint8_t disc_version) {
    if (server == WfcServer::Off || game_id.size() < 4) return;
    const bool mkw = game_id.compare(0, 3, "RMC") == 0;
    const char region = game_id[3];
    logf("WFC: %s for %s\n", to_string(server), game_id.c_str());
    if (server == WfcServer::WiiLink) {
        WiiLink(game_id, disc_version);
        return;
    }
    if (mkw && server == WfcServer::Wiimmfi) {
        // Wiimmfi's own update fixes the RCE hole, so no MkwRceFix here.
        if (!MkwWiimmfi(region)) logf("WFC: no Wiimmfi patch for Mario Kart Wii region %c\n", region);
        return;
    }
    unsigned https = 0, domains = 0;
    int generic = -1;
    for (const MemoryRegion& r : loaded) {
        std::uint8_t* bytes = reinterpret_cast<std::uint8_t*>(r.address);
        https += patch_https_to_http(bytes, r.length);
        domains += patch_wfc_domain(bytes, r.length, domain);
        if (server == WfcServer::Wiimmfi) {
            const int result = patch_wiimmfi_generic(bytes, r.length);
            if (generic != 0) generic = result;
        }
        Flush(r.address, r.length);
    }
    logf("WFC: %u https address(es) made http, %u server name(s) changed to %s\n", https, domains, domain.c_str());
    if (server == WfcServer::Wiimmfi) logf("WFC: Wiimmfi's game patch: %d (0 is done)\n", generic);
    // Other servers do not fix Mario Kart Wii's RCE hole themselves.
    if (mkw && server != WfcServer::Wiimmfi) MkwRceFix(region);
}

}  // namespace riftwii::wii
