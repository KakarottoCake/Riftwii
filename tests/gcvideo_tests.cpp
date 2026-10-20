// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A GameCube game's picture settings: what the loader finds in its code
// (riftwii/gcvideo.hpp). The code here is made up instruction by
// instruction in the shape the SDK's functions have.
#include "riftwii/gcvideo.hpp"

#include "gc_video.h"

#include <cstdint>
#include <iostream>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

constexpr std::uint32_t kBase = 0x80200000;
constexpr std::uint32_t kBlr = 0x4E800020;
constexpr std::uint32_t kNop = 0x60000000;

std::uint32_t lis(unsigned rd, std::uint16_t value) { return (15u << 26) | (rd << 21) | value; }
std::uint32_t addi(unsigned rd, unsigned ra, std::uint16_t value) { return (14u << 26) | (rd << 21) | (ra << 16) | value; }
std::uint32_t beq(std::uint32_t from, std::uint32_t to) { return (16u << 26) | (12u << 21) | (2u << 16) | ((to - from) & 0xFFFC); }
std::uint32_t bne(std::uint32_t from, std::uint32_t to) { return (16u << 26) | (4u << 21) | (2u << 16) | ((to - from) & 0xFFFC); }

// Code at kBase, as big-endian bytes.
struct Text {
    std::vector<std::uint32_t> words;
    std::vector<std::uint8_t> bytes;
    std::uint32_t here() const { return kBase + 4 * static_cast<std::uint32_t>(words.size()); }
    void add(std::uint32_t w) { words.push_back(w); }
    std::vector<riftwii::CodeRange> ranges() {
        bytes.clear();
        for (std::uint32_t w : words) {
            bytes.push_back(static_cast<std::uint8_t>(w >> 24));
            bytes.push_back(static_cast<std::uint8_t>(w >> 16));
            bytes.push_back(static_cast<std::uint8_t>(w >> 8));
            bytes.push_back(static_cast<std::uint8_t>(w));
        }
        return {riftwii::CodeRange{kBase, bytes.data(), bytes.size()}};
    }
};

// GXSetCopyFilter's shape: the test of vf, the sample pattern's writes,
// `beq` to the sharp weights, the weights from vfilter, then the sharp
// ones. `sharp_first` puts the sharp weights' halves in the other order.
// Returns where the `beq` is.
std::uint32_t add_copy_filter(Text& t, bool sharp_first = false) {
    t.add(kBlr);  // the function before
    t.add(0x54A0063F);  // clrlwi. r0,r5,24 (vf)
    for (int i = 0; i < 8; ++i) t.add(kNop);  // the sample pattern's writes
    const std::uint32_t branch = t.here();
    t.add(0);  // the beq, once the target is known
    for (int i = 0; i < 6; ++i) t.add(kNop);  // vfilter's weights
    t.add(0x48000014);  // b past the sharp weights
    const std::uint32_t sharp = t.here();
    t.add(lis(4, 0x5359));
    t.add(lis(3, 0x5400));
    if (sharp_first) {
        t.add(addi(7, 3, 0x0015));
        t.add(addi(6, 4, 0x5000));
    } else {
        t.add(addi(6, 4, 0x5000));
        t.add(addi(7, 3, 0x0015));
    }
    t.add(kBlr);
    t.words[(branch - kBase) / 4] = beq(branch, sharp);
    return branch;
}

void test_copy_filter() {
    {
        Text t;
        const std::uint32_t branch = add_copy_filter(t);
        const riftwii::GcCopyFilterSwitch found = riftwii::find_gc_copy_filter_switch(t.ranges());
        EXPECT_EQ(found.branch, branch);
        EXPECT_EQ(found.sharp, branch + 4 * 8);
    }
    {
        Text t;
        const std::uint32_t branch = add_copy_filter(t, true);
        EXPECT_EQ(riftwii::find_gc_copy_filter_switch(t.ranges()).branch, branch);
    }
    {
        // Two of them: which is the game's is not known, so neither.
        Text t;
        add_copy_filter(t);
        add_copy_filter(t);
        EXPECT_EQ(riftwii::find_gc_copy_filter_switch(t.ranges()).branch, 0u);
    }
    {
        // The weights reached by a bne, or by nothing: not this function.
        Text t;
        const std::uint32_t branch = add_copy_filter(t);
        t.words[(branch - kBase) / 4] = bne(branch, branch + 4 * 8);
        EXPECT_EQ(riftwii::find_gc_copy_filter_switch(t.ranges()).branch, 0u);
    }
    {
        // The sharp words alone (only one half made): not it.
        Text t;
        const std::uint32_t branch = add_copy_filter(t);
        t.words[(branch - kBase) / 4 + 10] = kNop;
        EXPECT_EQ(riftwii::find_gc_copy_filter_switch(t.ranges()).branch, 0u);
    }
    {
        Text t;
        t.add(kBlr);
        EXPECT_EQ(riftwii::find_gc_copy_filter_switch(t.ranges()).branch, 0u);
    }
}

std::uint32_t lbz(unsigned rd, unsigned ra, std::uint16_t offset) { return (34u << 26) | (rd << 21) | (ra << 16) | offset; }
std::uint32_t lwz(unsigned rd, unsigned ra, std::uint16_t offset) { return (32u << 26) | (rd << 21) | (ra << 16) | offset; }
// rlwinm rA,rS,shift,31,31: takes one bit (rS is the source, in rD's place).
std::uint32_t take_bit(unsigned ra, unsigned rs, unsigned shift) {
    return (21u << 26) | (rs << 21) | (ra << 16) | (shift << 11) | (31u << 6) | (31u << 1);
}
// rlwinm rDest,rSrc,shift,first,last, built from its fields.
std::uint32_t rlwinm_word(unsigned dest, unsigned src, unsigned shift, unsigned first, unsigned last) {
    return (21u << 26) | (src << 21) | (dest << 16) | (shift << 11) | (first << 6) | (last << 1);
}
std::uint32_t oris_word(unsigned dest, unsigned src, std::uint16_t value) {
    return (25u << 26) | (src << 21) | (dest << 16) | value;
}
std::uint32_t sth_word(unsigned src, unsigned base, std::uint16_t offset) {
    return (44u << 26) | (src << 21) | (base << 16) | offset;
}

std::uint32_t word_at_bytes(const std::vector<std::uint8_t>& bytes, std::size_t index) {
    return (std::uint32_t(bytes[4 * index]) << 24) | (std::uint32_t(bytes[4 * index + 1]) << 16) |
           (std::uint32_t(bytes[4 * index + 2]) << 8) | bytes[4 * index + 3];
}

// A getter of the console's settings, as the SDK's are built: the copy's
// address (lis/addi), the lock (lwz r0,0x48 and addi r4,0x48), the byte,
// then, when `shift` is not 0, its bit taken. Returns where the load is.
std::uint32_t add_sram_getter(Text& t, std::uint32_t copy, std::uint16_t offset, unsigned shift) {
    const unsigned base = 31;
    t.add(kBlr);
    t.add(lis(3, static_cast<std::uint16_t>((copy + 0x8000) >> 16)));
    t.add(addi(base, 3, static_cast<std::uint16_t>(copy & 0xFFFF)));
    t.add(kNop);  // bl OSDisableInterrupts
    t.add(lwz(0, base, 0x48));
    t.add(addi(4, base, 0x48));
    t.add(kNop);
    const std::uint32_t load = t.here();
    t.add(lbz(0, base, offset));
    t.add(kNop);
    if (shift != 0) t.add(take_bit(base, 0, shift));
    t.add(kBlr);
    return load;
}

void test_sram_reads() {
    constexpr std::uint32_t kCopy = 0x804B8780;
    Text t;
    const std::uint32_t language = add_sram_getter(t, kCopy, 0x12, 0);
    const std::uint32_t progressive = add_sram_getter(t, kCopy, 0x13, 25);  // bit 7
    const std::uint32_t sixty = add_sram_getter(t, kCopy, 0x11, 26);        // bit 6
    add_sram_getter(t, kCopy, 0x13, 0);  // a setter reading the byte to change it
    add_sram_getter(t, kCopy, 0x11, 25);  // another bit of 0x11: not the 60 Hz one
    const riftwii::GcSramReads reads = riftwii::find_gc_sram_reads(t.ranges());
    EXPECT_EQ(reads.copy, kCopy);
    EXPECT_EQ(reads.language.size(), 1u);
    EXPECT_EQ(reads.progressive.size(), 1u);
    EXPECT_EQ(reads.sixty_hz.size(), 1u);
    if (reads.language.size() == 1) EXPECT_EQ(reads.language[0], language);
    if (reads.progressive.size() == 1) EXPECT_EQ(reads.progressive[0], progressive);
    if (reads.sixty_hz.size() == 1) EXPECT_EQ(reads.sixty_hz[0], sixty);
    // A game without the 60 Hz getter (Luigi's Mansion's SDK): none.
    Text old;
    add_sram_getter(old, kCopy, 0x13, 25);
    EXPECT_EQ(riftwii::find_gc_sram_reads(old.ranges()).sixty_hz.size(), 0u);
    EXPECT_EQ(riftwii::find_gc_sram_reads(old.ranges()).progressive.size(), 1u);
}

// A render mode as the SDK's tables hold one: TV mode, fbWidth, then the
// VI's origin and width, xfbMode and field rendering.
struct Mode {
    std::uint8_t bytes[GC_RM_BYTES] = {};
    Mode(std::uint32_t tv_mode, unsigned fb_width, unsigned x, unsigned width, std::uint32_t xfb_mode,
         std::uint8_t field_rendering = 0) {
        gc_video_put32(bytes + GC_RM_TV_MODE, tv_mode);
        gc_video_put16(bytes + GC_RM_FB_WIDTH, fb_width);
        gc_video_put16(bytes + GC_RM_VI_X, x);
        gc_video_put16(bytes + GC_RM_VI_WIDTH, width);
        gc_video_put32(bytes + GC_RM_XFB_MODE, xfb_mode);
        bytes[GC_RM_FIELD_RENDERING] = field_rendering;
        bytes[50] = 21;  // something past the fields changed, copied as it is
    }
};

constexpr std::uint32_t kPalInterlaced = 1u << 2;

void test_video_pick() {
    std::uint8_t copy[GC_RM_BYTES];
    {
        // Nothing asked: the game's own mode, copied whole.
        const Mode ntsc(GC_VI_NTSC_INTERLACED, 640, 40, 640, GC_VI_XFB_DOUBLE);
        EXPECT_EQ(gc_video_pick(ntsc.bytes, 0, 0, copy), 0u);
        EXPECT_EQ(unsigned(copy[50]), 21u);
    }
    {
        // 480p: interlaced whole frames become progressive, single field.
        const Mode ntsc(GC_VI_NTSC_INTERLACED, 640, 40, 640, GC_VI_XFB_DOUBLE);
        EXPECT_EQ(gc_video_pick(ntsc.bytes, 1, 0, copy), 1u);
        EXPECT_EQ(gc_video_get32(copy + GC_RM_TV_MODE), GC_VI_NTSC_PROGRESSIVE);
        EXPECT_EQ(gc_video_get32(copy + GC_RM_XFB_MODE), GC_VI_XFB_SINGLE);
        EXPECT_EQ(gc_video_get16(copy + GC_RM_VI_WIDTH), 640u);
        // A field at a time, or a PAL mode: left alone.
        const Mode fields(GC_VI_NTSC_INTERLACED, 640, 40, 640, GC_VI_XFB_DOUBLE, 1);
        EXPECT_EQ(gc_video_pick(fields.bytes, 1, 0, copy), 0u);
        const Mode pal(kPalInterlaced, 640, 40, 640, GC_VI_XFB_DOUBLE);
        EXPECT_EQ(gc_video_pick(pal.bytes, 1, 0, copy), 0u);
    }
    {
        // Picture width: drawn that wide, centred in the 720-pixel line.
        const Mode ntsc(GC_VI_NTSC_INTERLACED, 640, 40, 640, GC_VI_XFB_DOUBLE);
        EXPECT_EQ(gc_video_pick(ntsc.bytes, 0, 720, copy), 1u);
        EXPECT_EQ(gc_video_get16(copy + GC_RM_VI_WIDTH), 720u);
        EXPECT_EQ(gc_video_get16(copy + GC_RM_VI_X), 0u);
        EXPECT_EQ(gc_video_get32(copy + GC_RM_TV_MODE), GC_VI_NTSC_INTERLACED);
        EXPECT_EQ(gc_video_pick(ntsc.bytes, 0, 704, copy), 1u);
        EXPECT_EQ(gc_video_get16(copy + GC_RM_VI_WIDTH), 704u);
        EXPECT_EQ(gc_video_get16(copy + GC_RM_VI_X), 8u);
        // 704 asked for with a 720-wide frame buffer: nothing to widen,
        // and the trace's flag says the setting was kept.
        const Mode wide(GC_VI_NTSC_INTERLACED, 720, 0, 720, GC_VI_XFB_DOUBLE);
        EXPECT_EQ(gc_video_pick(wide.bytes, 0, 704, copy), 0u);
        EXPECT_EQ(gc_video_width_kept(wide.bytes, 704), 1u);
        EXPECT_EQ(gc_video_width_kept(ntsc.bytes, 704), 0u);
        EXPECT_EQ(gc_video_width_kept(wide.bytes, GC_VI_WIDTH_FRAMEBUFFER), 0u);
        EXPECT_EQ(gc_video_width_kept(wide.bytes, 0), 0u);
        // Both at once.
        EXPECT_EQ(gc_video_pick(ntsc.bytes, 1, 720, copy), 1u);
        EXPECT_EQ(gc_video_get32(copy + GC_RM_TV_MODE), GC_VI_NTSC_PROGRESSIVE);
        EXPECT_EQ(gc_video_get16(copy + GC_RM_VI_WIDTH), 720u);
        // Already that wide: nothing to change.
        const Mode full(GC_VI_NTSC_INTERLACED, 640, 0, 720, GC_VI_XFB_DOUBLE);
        EXPECT_EQ(gc_video_pick(full.bytes, 0, 720, copy), 0u);
    }
    {
        // The frame buffer's width: Luigi's Mansion's PAL picture is 640
        // pixels drawn 660 wide at 30; it becomes 640 at 40.
        const Mode lm(kPalInterlaced, 640, 30, 660, GC_VI_XFB_DOUBLE);
        EXPECT_EQ(gc_video_pick(lm.bytes, 0, GC_VI_WIDTH_FRAMEBUFFER, copy), 1u);
        EXPECT_EQ(gc_video_get16(copy + GC_RM_VI_WIDTH), 640u);
        EXPECT_EQ(gc_video_get16(copy + GC_RM_VI_X), 40u);
        // Narrower than the frame buffer: the VI cannot shrink it.
        const Mode wide(GC_VI_NTSC_INTERLACED, 704, 8, 704, GC_VI_XFB_DOUBLE);
        EXPECT_EQ(gc_video_pick(wide.bytes, 0, 640, copy), 0u);
    }
    EXPECT_EQ(riftwii::gc_vi_width("game"), 0u);
    EXPECT_EQ(riftwii::gc_vi_width("framebuffer"), GC_VI_WIDTH_FRAMEBUFFER);
    EXPECT_EQ(riftwii::gc_vi_width("704"), 704u);
    EXPECT_EQ(riftwii::gc_vi_width("720"), 720u);
    EXPECT_EQ(riftwii::gc_vi_width("1080"), 0u);
}

// GXInitTlutObj's stale palette address (riftwii/gcvideo.hpp): every word
// built from its fields, never copied from a game.
void test_tlut() {
    // Mario Party 4's shape: the `lwz` and the `rlwinm` that keeps the old
    // word's top bits, then the `oris` with 0x6400 and the `sth` of the
    // entry count, at the same spacing as the game's function (other words
    // plain nops, with the function's two other masks as decoys beside).
    auto add_mp4 = [](Text& t) {
        for (int i = 0; i < 8; ++i) t.add(kNop);
        t.add(lwz(4, 3, 4));                      // index 8
        t.add(rlwinm_word(4, 4, 0, 0, 10));       // index 9: keeps the old top 11 bits
        t.add(rlwinm_word(0, 4, 27, 7, 31));      // a decoy mask from the same function
        t.add(rlwinm_word(4, 7, 0, 22, 19));      // another decoy mask
        t.add(kNop);
        t.add(kNop);
        t.add(oris_word(0, 0, 0x6400));           // index 14
        t.add(kNop);
        t.add(sth_word(6, 3, 8));                 // index 16
        t.add(kBlr);
    };
    {
        Text t;
        add_mp4(t);
        std::vector<riftwii::CodeRange> ranges = t.ranges();
        const riftwii::GcTlutSites sites = riftwii::find_gc_tlut_sites(ranges);
        EXPECT_EQ(sites.stale.size(), 1u);
        EXPECT_EQ(sites.clean.size(), 0u);
        if (sites.stale.size() == 1) EXPECT_EQ(sites.stale[0], kBase + 4 * 9);
        EXPECT_EQ(riftwii::gc_tlut_li_for(word_at_bytes(t.bytes, 9)), addi(4, 0, 0));
        const std::vector<std::uint32_t> patched = riftwii::patch_gc_tlut(ranges);
        EXPECT_EQ(patched.size(), 1u);
        EXPECT_EQ(word_at_bytes(t.bytes, 9), 0x38800000u);
        EXPECT_EQ(word_at_bytes(t.bytes, 14), oris_word(0, 0, 0x6400));
        const riftwii::GcTlutSites after = riftwii::find_gc_tlut_sites(ranges);
        EXPECT_EQ(after.stale.size(), 0u);
        EXPECT_EQ(after.clean.size(), 1u);
    }
    {
        // Another compiler's registers: the shape matches by field, and the
        // replacement clears the same register the load read.
        Text t;
        t.add(lwz(9, 3, 4));
        t.add(rlwinm_word(9, 9, 0, 0, 10));
        t.add(kNop);
        t.add(oris_word(7, 7, 0x6400));
        t.add(kNop);
        t.add(sth_word(6, 3, 8));
        t.add(kBlr);
        std::vector<riftwii::CodeRange> ranges = t.ranges();
        const riftwii::GcTlutSites sites = riftwii::find_gc_tlut_sites(ranges);
        EXPECT_EQ(sites.stale.size(), 1u);
        EXPECT_EQ(riftwii::gc_tlut_li_for(word_at_bytes(t.bytes, 1)), addi(9, 0, 0));
        EXPECT_EQ(riftwii::patch_gc_tlut(ranges).size(), 1u);
        EXPECT_EQ(word_at_bytes(t.bytes, 1), addi(9, 0, 0));
    }
    {
        // Already clean: the load is followed by `li` instead of the keep.
        Text t;
        t.add(lwz(4, 3, 4));
        t.add(addi(4, 0, 0));
        t.add(kNop);
        t.add(oris_word(0, 0, 0x6400));
        t.add(sth_word(6, 3, 8));
        t.add(kBlr);
        std::vector<riftwii::CodeRange> ranges = t.ranges();
        const riftwii::GcTlutSites sites = riftwii::find_gc_tlut_sites(ranges);
        EXPECT_EQ(sites.stale.size(), 0u);
        EXPECT_EQ(sites.clean.size(), 1u);
        EXPECT_EQ(riftwii::patch_gc_tlut(ranges).size(), 0u);
    }
    {
        // No `oris` with 0x6400 nearby: not this function.
        Text t;
        add_mp4(t);
        t.words[14] = oris_word(0, 0, 0x6300);
        std::vector<riftwii::CodeRange> ranges = t.ranges();
        EXPECT_EQ(riftwii::find_gc_tlut_sites(ranges).stale.size(), 0u);
        EXPECT_EQ(riftwii::patch_gc_tlut(ranges).size(), 0u);
    }
    {
        // Another mask (last bit 11, not 10): the old word is not kept.
        Text t;
        add_mp4(t);
        t.words[9] = rlwinm_word(4, 4, 0, 0, 11);
        std::vector<riftwii::CodeRange> ranges = t.ranges();
        EXPECT_EQ(riftwii::find_gc_tlut_sites(ranges).stale.size(), 0u);
        EXPECT_EQ(riftwii::patch_gc_tlut(ranges).size(), 0u);
    }
    {
        // The load reads another word of the object: not the address word.
        Text t;
        add_mp4(t);
        t.words[8] = lwz(4, 3, 8);
        std::vector<riftwii::CodeRange> ranges = t.ranges();
        EXPECT_EQ(riftwii::find_gc_tlut_sites(ranges).stale.size(), 0u);
        EXPECT_EQ(riftwii::patch_gc_tlut(ranges).size(), 0u);
    }
}

}  // namespace

int main() {
    test_copy_filter();
    test_video_pick();
    test_sram_reads();
    test_tlut();
    if (g_failures == 0) std::cout << "gcvideo tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
