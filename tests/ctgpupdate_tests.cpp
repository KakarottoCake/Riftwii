// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// CTGP-R's update container and rebuild stream (riftwii/ctgpupdate.hpp), with
// synthetic data built to the format the channel's own code reads.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgpupdate.hpp"
#include "riftwii/ctgpyaz0.hpp"
#include "riftwii/sha1.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(std::uint8_t(value >> 24));
    out.push_back(std::uint8_t(value >> 16));
    out.push_back(std::uint8_t(value >> 8));
    out.push_back(std::uint8_t(value));
}

void put_name(std::vector<std::uint8_t>& out, const char* name) {
    std::uint8_t field[8] = {};
    std::memcpy(field, name, std::min<std::size_t>(std::strlen(name), 8));
    out.insert(out.end(), field, field + 8);
}

std::vector<std::uint8_t> bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

class MemoryBase : public CtgpBaseSource {
public:
    explicit MemoryBase(std::string data) : data_(std::move(data)) {}
    std::uint32_t size() const override { return std::uint32_t(data_.size()); }
    bool read(std::uint32_t offset, std::uint8_t* out, std::size_t length) const override {
        if (std::uint64_t(offset) + length > data_.size()) {
            return false;
        }
        std::memcpy(out, data_.data() + offset, length);
        return true;
    }

private:
    std::string data_;
};

// A base file the card will not give: right size, every read fails.
class UnreadableBase : public CtgpBaseSource {
public:
    std::uint32_t size() const override { return 7; }
    bool read(std::uint32_t, std::uint8_t*, std::size_t) const override { return false; }
};

bool collect(void* user, const std::uint8_t* data, std::size_t length) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(user);
    out->insert(out->end(), data, data + length);
    return true;
}

// A DIF0 whose one entry rebuilds from a single base file.
std::vector<std::uint8_t> make_section(std::uint32_t unpacked, const std::vector<std::uint8_t>& stream,
                                       const std::string& path) {
    const std::uint32_t header = 0x30;
    const std::uint32_t path_table = header + std::uint32_t(stream.size());
    const std::uint32_t total = path_table + std::uint32_t(path.size()) + 1;
    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'D', 'I', 'F', '0'});
    put32(out, total);
    put32(out, 0);
    put32(out, path_table);
    put32(out, header);
    put32(out, 0x00020001u);
    put32(out, unpacked);
    put32(out, 1);
    put_name(out, "CTGPR");
    put32(out, path_table);
    put32(out, 1);
    out.insert(out.end(), stream.begin(), stream.end());
    out.insert(out.end(), path.begin(), path.end());
    out.push_back(0);
    return out;
}

std::uint32_t be32_at(const std::vector<std::uint8_t>& d, std::size_t at) {
    return (std::uint32_t(d[at]) << 24) | (std::uint32_t(d[at + 1]) << 16) | (std::uint32_t(d[at + 2]) << 8) | d[at + 3];
}

// A DIA0 holding `sections` one after another. `listed` (when not 0) is the
// count its table claims, for a table that leaves sections out; the header's
// unpacked size is always the sum of all the sections'.
std::vector<std::uint8_t> make_dia(const std::vector<std::vector<std::uint8_t>>& sections, std::uint32_t listed = 0) {
    const std::uint32_t count = listed != 0 ? listed : std::uint32_t(sections.size());
    const std::uint32_t base_offset = 0x64;
    const std::uint32_t table_offset = base_offset + 16;
    const std::uint32_t first_section = table_offset + 8 * std::uint32_t(sections.size());
    std::uint32_t total = first_section;
    std::uint32_t unpacked = 0;
    for (const auto& section : sections) {
        total += std::uint32_t(section.size());
        unpacked += be32_at(section, 0x18);
    }
    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'D', 'I', 'A', '0'});
    put32(out, total);
    put32(out, 0);
    put32(out, 0);
    put_name(out, "CTGPR");
    put32(out, 0x01031193u);
    put32(out, unpacked);
    put32(out, 0);  // signature length
    while (out.size() < 0x54) {
        out.push_back(0);
    }
    put32(out, 1);            // base records
    put32(out, base_offset);
    put32(out, count);        // sections
    put32(out, table_offset);
    put_name(out, "CTGPR");
    put32(out, 0x01031192u);
    put32(out, 0);
    std::uint32_t at = first_section;
    for (const auto& section : sections) {
        put32(out, at);
        put32(out, std::uint32_t(section.size()));
        at += std::uint32_t(section.size());
    }
    for (const auto& section : sections) out.insert(out.end(), section.begin(), section.end());
    return out;
}

std::vector<std::uint8_t> make_dia(const std::vector<std::uint8_t>& section) {
    return make_dia(std::vector<std::vector<std::uint8_t>>{section});
}

void test_yaz0_round_trip() {
    std::string text;
    for (int i = 0; i < 400; ++i) {
        text.push_back(char('A' + (i % 7)));
        if (i % 23 == 0) {
            text += "the quick brown fox";
        }
    }
    const auto input = bytes(text);
    std::vector<std::uint8_t> packed;
    std::string error;
    EXPECT_TRUE(ctgp_yaz0_encode_exact(input.data(), input.size(), packed, error));
    std::vector<std::uint8_t> unpacked;
    EXPECT_TRUE(ctgp_yaz0_decode_nintendo(packed.data(), packed.size(), unpacked, error));
    EXPECT_TRUE(unpacked == input);
}

void test_dia_parse() {
    const std::vector<std::uint8_t> stream = {0x40, 'h', 'e', 'l', 'l', 'o'};
    const auto section = make_section(5, stream, "NEW.SZS");
    const auto dia = make_dia(section);
    CtgpDia parsed;
    std::string error;
    EXPECT_TRUE(ctgp_dia_parse(dia.data(), dia.size(), parsed, error));
    EXPECT_EQ(parsed.component, std::string("CTGPR"));
    EXPECT_EQ(parsed.version, 0x01031193u);
    EXPECT_EQ(parsed.bases.size(), std::size_t(1));
    EXPECT_EQ(parsed.bases[0].component, std::string("CTGPR"));
    EXPECT_EQ(parsed.bases[0].version, 0x01031192u);
    EXPECT_EQ(parsed.sections.size(), std::size_t(1));
    EXPECT_EQ(parsed.sections[0].offset, 0x7Cu);
    EXPECT_EQ(parsed.sections[0].size, std::uint32_t(section.size()));
    // A truncated download is refused.
    CtgpDia again;
    EXPECT_TRUE(!ctgp_dia_parse(dia.data(), dia.size() - 1, again, error));

    // Two files, and a table whose count says one: the length and the first
    // section still check out, but the second file's bytes are left over, so
    // the update is refused rather than installed with a file missing.
    const auto second = make_section(5, stream, "TWO.SZS");
    const auto both = make_dia({section, second});
    EXPECT_TRUE(ctgp_dia_parse(both.data(), both.size(), again, error));
    EXPECT_EQ(again.sections.size(), std::size_t(2));
    const auto short_table = make_dia({section, second}, 1);
    EXPECT_TRUE(!ctgp_dia_parse(short_table.data(), short_table.size(), again, error));
    // Sections that overlap or leave a gap are refused too.
    auto gap = both;
    gap[0x74 + 8 + 3] += 1;  // the second section starts one byte late
    EXPECT_TRUE(!ctgp_dia_parse(gap.data(), gap.size(), again, error));
}

void test_apply() {
    // hello + seven bytes of base + five repeats of the last base byte (a
    // window copy with no distance set repeats the previous byte).
    std::vector<std::uint8_t> stream = {0x40, 'h', 'e', 'l', 'l', 'o',
                                        0x14, 1,
                                        0x33};
    const auto section = make_section(17, stream, "NEW.SZS");
    CtgpDif dif;
    std::string error;
    EXPECT_TRUE(ctgp_dif_parse(section.data(), section.size(), dif, error));
    EXPECT_EQ(dif.unpacked_size, 17u);
    EXPECT_EQ(dif.entries.size(), std::size_t(1));
    EXPECT_EQ(ctgp_dif_path(section.data(), section.size(), dif.path_table), std::string("NEW.SZS"));
    EXPECT_EQ(ctgp_dif_path(section.data(), section.size(), dif.entries[0].path_offset), std::string("NEW.SZS"));

    MemoryBase base("ABCDEFG");
    std::vector<const CtgpBaseSource*> bases = {&base};
    std::vector<std::uint8_t> output;
    EXPECT_TRUE(ctgp_dif_apply(section.data(), section.size(), dif, bases, collect, &output, error));
    EXPECT_EQ(output, bytes("helloABCDEFGGGGGG"));
}

void test_apply_distance() {
    // A window copy reads from the distance + 1 bytes back, and the distance
    // lasts for one copy: "abc", distance 2 then copy 3 gives "abc" again;
    // the next copy, with no distance, repeats the last byte; a literal also
    // clears it.
    std::vector<std::uint8_t> stream = {0x20, 'a', 'b', 'c',
                                        0x18, 0x13,          // distance 2, copy 3: abc
                                        0x13,                // copy 3 of the last byte: ccc
                                        0x28, 0x00, 'q',     // distance 3, then a literal clears it
                                        0x13};               // qqq
    const std::uint32_t size = 3 + 3 + 3 + 1 + 3;
    auto section = make_section(size, stream, "NEW.SZS");
    CtgpDif dif;
    std::string error;
    EXPECT_TRUE(ctgp_dif_parse(section.data(), section.size(), dif, error));
    MemoryBase base("unused");
    std::vector<const CtgpBaseSource*> bases = {&base};
    std::vector<std::uint8_t> output;
    EXPECT_TRUE(ctgp_dif_apply(section.data(), section.size(), dif, bases, collect, &output, error));
    EXPECT_EQ(output, bytes("abcabccccqqqq"));

    // A distance that reaches before the start is refused.
    stream = {0x00, 'a', 0x18, 0x13};
    section = make_section(4, stream, "NEW.SZS");
    EXPECT_TRUE(ctgp_dif_parse(section.data(), section.size(), dif, error));
    output.clear();
    EXPECT_TRUE(!ctgp_dif_apply(section.data(), section.size(), dif, bases, collect, &output, error));
}

void test_apply_full_window() {
    // A copy may reach back exactly the window's size (8192 bytes; distance
    // 8191, set with the 0xA form), but not one byte further.
    std::vector<std::uint8_t> stream = {0x11, 0x1E, 0xEF};  // a literal of 0x111 + 0x1EEF = 8192 bytes
    for (std::uint32_t i = 0; i < kCtgpUpdateWindowBytes; ++i) stream.push_back(std::uint8_t(i * 7 + 3));
    const std::size_t literal_end = stream.size();
    stream.insert(stream.end(), {0x0A, 0x1E, 0xFE, 0x23});  // distance 0x101 + 0x1EFE = 8191, copy 4
    auto section = make_section(kCtgpUpdateWindowBytes + 4, stream, "NEW.SZS");
    CtgpDif dif;
    std::string error;
    EXPECT_TRUE(ctgp_dif_parse(section.data(), section.size(), dif, error));
    MemoryBase base("unused");
    std::vector<const CtgpBaseSource*> bases = {&base};
    std::vector<std::uint8_t> output;
    EXPECT_TRUE(ctgp_dif_apply(section.data(), section.size(), dif, bases, collect, &output, error));
    EXPECT_EQ(output.size(), std::size_t(kCtgpUpdateWindowBytes + 4));
    if (output.size() == kCtgpUpdateWindowBytes + 4) {
        EXPECT_TRUE(std::equal(output.begin(), output.begin() + 4, output.begin() + kCtgpUpdateWindowBytes));
    }

    stream[literal_end + 2] = 0xFF;  // distance 8192: past the window
    section = make_section(kCtgpUpdateWindowBytes + 4, stream, "NEW.SZS");
    EXPECT_TRUE(ctgp_dif_parse(section.data(), section.size(), dif, error));
    output.clear();
    EXPECT_TRUE(!ctgp_dif_apply(section.data(), section.size(), dif, bases, collect, &output, error));
}

// A two-file DIA0 for the signature tests, signed (by sign_test_dia) with a
// throwaway DSA test key: CTGP-R's own key can only check, never sign.
std::vector<std::uint8_t> signed_test_dia_unsigned() {
    const std::vector<std::uint8_t> stream = {0x40, 'h', 'e', 'l', 'l', 'o'};
    return make_dia({make_section(5, stream, "NEW.SZS"), make_section(5, stream, "TWO.SZS")});
}

std::vector<std::uint8_t> from_hex(const char* text) {
    std::vector<std::uint8_t> out;
    for (; text[0] && text[1]; text += 2) {
        auto nibble = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
        out.push_back(std::uint8_t(nibble(text[0]) * 16 + nibble(text[1])));
    }
    return out;
}

// The throwaway test key's public half (p, q, g, y) and its signature of
// signed_test_dia_unsigned(), made once off-line for this test.
const char* const kTestDsaP =
        "801256947dfccd8ff8147518fd464cf127cb4932af3e0a7d6a810574adadc4c8"
        "ea97f69a4d2a6b77d134f32db2e4101a3fbec2d27507aabd342a51ceb6560107";
const char* const kTestDsaQ = "9b53fa398b9f3e4d3ff352b16b6c6df7268ce515";
const char* const kTestDsaG =
        "5b6a18977e127b870fab45bec39e8df659f1c212b2f9ad5c32e9e6f24eabb7d5"
        "d454f24fb029e78c4a96fce217a6f8f351e742a78ff53f2bf7cbd46e1296a7c4";
const char* const kTestDsaY =
        "21f92ddc1dd7f75d9229d5cb540591bf9a9e813fa53a991753e489589b379de2"
        "4891ba55c7cda314944aed1ec019760f1aafcedd1429b9fa86f7664e8ae08f25";
const char* const kTestDiaSignature =
        "302c0214768a3b024fababfce9bde832020560704baf9ec0021401c9efcfcc37"
        "962239fee2e667d38695ab4ec32c";

void test_dia_signature() {
    CtgpDsaKey key{};
    const auto p = from_hex(kTestDsaP), q = from_hex(kTestDsaQ), g = from_hex(kTestDsaG), y = from_hex(kTestDsaY);
    std::memcpy(key.p, p.data(), 64);
    std::memcpy(key.q, q.data(), 20);
    std::memcpy(key.g, g.data(), 64);
    std::memcpy(key.y, y.data(), 64);
    const auto signature = from_hex(kTestDiaSignature);
    auto dia = signed_test_dia_unsigned();
    dia[0x23] = std::uint8_t(signature.size());
    std::copy(signature.begin(), signature.end(), dia.begin() + 0x24);
    auto verify = [&](const std::vector<std::uint8_t>& file, const CtgpDsaKey& with) {
        std::string error;
        const CtgpUpdateRead read = [&file](std::uint64_t offset, std::uint8_t* out, std::size_t length) {
            if (offset + length > file.size()) return false;
            std::memcpy(out, file.data() + offset, length);
            return true;
        };
        return ctgp_dia_verify_signature(read, file.size(), with, nullptr, error);
    };
    EXPECT_TRUE(verify(dia, key));
    // Another key (CTGP-R's own) does not vouch for it.
    EXPECT_TRUE(!verify(dia, ctgp_signing_key()));

    // The change review found the table checks alone let through: the table
    // moved up and cut to one entry, that section stretched over the rest of
    // the file, and its own size field to match. The tables still tile the
    // file and the first file still checks out, so it parses; the signature
    // is what refuses it.
    auto tampered = dia;
    const std::uint32_t total = std::uint32_t(tampered.size());
    auto set32 = [&](std::size_t at, std::uint32_t value) {
        tampered[at] = std::uint8_t(value >> 24); tampered[at + 1] = std::uint8_t(value >> 16);
        tampered[at + 2] = std::uint8_t(value >> 8); tampered[at + 3] = std::uint8_t(value);
    };
    set32(0x5C, 1);                    // one section
    set32(0x60, 0x7C);                 // the table, moved up to end where the first section starts
    set32(0x7C, 0x84);                 // that section's offset
    set32(0x80, total - 0x84);         // stretched to the end of the file
    set32(0x84 + 4, total - 0x84);     // and its DIF0 size field to match
    CtgpDia parsed;
    std::string error;
    EXPECT_TRUE(ctgp_dia_parse(tampered.data(), tampered.size(), parsed, error));
    EXPECT_EQ(parsed.sections.size(), std::size_t(1));
    EXPECT_TRUE(!verify(tampered, key));

    // The other change review found: the second file left out, its table
    // entry dropped, the rest moved up and every size fixed to match. The
    // shortened file is a well-formed one-file update (it parses), but it is
    // not what CTGP-R signed.
    const std::vector<std::uint8_t> stream = {0x40, 'h', 'e', 'l', 'l', 'o'};
    auto compacted = make_dia({make_section(5, stream, "NEW.SZS")});
    compacted[0x23] = std::uint8_t(signature.size());
    std::copy(signature.begin(), signature.end(), compacted.begin() + 0x24);
    EXPECT_TRUE(ctgp_dia_parse(compacted.data(), compacted.size(), parsed, error));
    EXPECT_TRUE(!verify(compacted, key));

    // Any other change is refused too: a byte of the last file, the count
    // alone, a byte added, and no signature at all.
    auto changed = dia;
    changed.back() ^= 0x01;
    EXPECT_TRUE(!verify(changed, key));
    changed = dia;
    changed[0x5F] = 1;
    EXPECT_TRUE(!verify(changed, key));
    changed = dia;
    changed.push_back(0);
    EXPECT_TRUE(!verify(changed, key));
    changed = dia;
    changed[0x23] = 0;
    EXPECT_TRUE(!verify(changed, key));
}

class MemoryTarget : public CtgpUpdateTarget {
public:
    bool load_base(const std::string& component, const std::string& path,
                   std::unique_ptr<CtgpBaseSource>& out, const CtgpProgress& progress,
                   std::string& error) override {
        if (component != "CTGPR" || path != "/Race/Course/NEW.szs") {
            error = "unexpected base " + component + " " + path;
            return false;
        }
        if (progress) progress(7, 7);
        if (fail_base_reads) {
            out = std::make_unique<UnreadableBase>();
        } else {
            out = std::make_unique<MemoryBase>("ABCDEFG");
        }
        return true;
    }
    bool check_file(const std::string& path, const std::array<std::uint8_t, 20>& digest,
                    std::string& error) override {
        checked.push_back(path);
        last_digest = digest;
        if (refuse) {
            error = "refused";
            return false;
        }
        return true;
    }
    bool store_file(const std::string& path, const CtgpPieces& packed, const CtgpProgress& progress,
                    std::string& error) override {
        if (progress) progress(packed.size(), packed.size());
        if (path != "/Race/Course/NEW.szs") {
            error = "unexpected output " + path;
            return false;
        }
        stored.assign(packed.size(), 0);
        return packed.read(0, stored.data(), stored.size());
    }
    std::vector<std::uint8_t> stored;
    std::vector<std::string> checked;
    std::string packed_scratch_path() const override { return scratch; }
    std::array<std::uint8_t, 20> last_digest{};
    bool refuse = false;
    bool fail_base_reads = false;
    std::string scratch;
};

// Why an apply failed: a section whose commands are broken is the update's
// own fault (set it aside); a base file the card could not read is not.
void test_apply_failure_kind() {
    auto run = [](const std::vector<std::uint8_t>& stream, std::uint32_t size, MemoryTarget& target,
                  CtgpUpdateFailure& failure) {
        const auto dia = make_dia(make_section(size, stream, "/Race/Course/NEW.szs"));
        const CtgpUpdateRead read = [&dia](std::uint64_t offset, std::uint8_t* out, std::size_t length) {
            if (offset + length > dia.size()) return false;
            std::memcpy(out, dia.data() + offset, length);
            return true;
        };
        std::string error;
        return ctgp_update_apply(read, dia.size(), target, nullptr, CtgpUpdateFileDone(), CtgpUpdateStep(), failure,
                                 error);
    };
    const std::vector<std::uint8_t> good = {0x40, 'h', 'e', 'l', 'l', 'o', 0x14, 1, 0x33};
    CtgpUpdateFailure failure = CtgpUpdateFailure::Temporary;
    MemoryTarget fine;
    EXPECT_TRUE(run(good, 17, fine, failure));
    EXPECT_TRUE(failure == CtgpUpdateFailure::None);

    MemoryTarget broken;
    EXPECT_TRUE(!run({0x55}, 1, broken, failure));  // no such opcode
    EXPECT_TRUE(failure == CtgpUpdateFailure::BadUpdate);
    EXPECT_TRUE(broken.stored.empty());
    EXPECT_TRUE(!run({0x14, 200}, 207, broken, failure));  // a base copy past the base's end
    EXPECT_TRUE(failure == CtgpUpdateFailure::BadUpdate);

    MemoryTarget card;
    card.fail_base_reads = true;
    EXPECT_TRUE(!run(good, 17, card, failure));
    EXPECT_TRUE(failure == CtgpUpdateFailure::Temporary);
    EXPECT_TRUE(card.stored.empty());

    MemoryTarget strict;  // the hash list does not vouch for it: maybe a newer list will
    strict.refuse = true;
    EXPECT_TRUE(!run(good, 17, strict, failure));
    EXPECT_TRUE(failure == CtgpUpdateFailure::Temporary);
}

void test_update_apply() {
    std::vector<std::uint8_t> stream = {0x40, 'h', 'e', 'l', 'l', 'o', 0x14, 1, 0x33};
    const auto section = make_section(17, stream, "/Race/Course/NEW.szs");
    const auto dia = make_dia(section);
    MemoryTarget target;
    std::string error;
    std::vector<std::string> lines;
    EXPECT_TRUE(ctgp_update_apply(dia.data(), dia.size(), target,
                                  [&](const std::string& line) { lines.push_back(line); }, error));
    std::vector<std::uint8_t> unpacked;
    EXPECT_TRUE(ctgp_yaz0_decode_nintendo(target.stored.data(), target.stored.size(), unpacked, error));
    EXPECT_EQ(unpacked, bytes("helloABCDEFGGGGGG"));
    EXPECT_TRUE(lines.size() >= 2);
    EXPECT_EQ(target.checked.size(), std::size_t(1));
    // The digest checked is the one of the bytes stored.
    EXPECT_TRUE(target.last_digest == sha1(target.stored.data(), target.stored.size()));

    // Kept in a scratch file until it is stored: the same bytes, and the
    // file is gone afterwards.
    MemoryTarget spilled;
    spilled.scratch = "ctgpupdate-packed.tmp";
    EXPECT_TRUE(ctgp_update_apply(dia.data(), dia.size(), spilled, [](const std::string&) {}, error));
    EXPECT_TRUE(spilled.stored == target.stored);
    EXPECT_TRUE(std::fopen(spilled.scratch.c_str(), "rb") == nullptr);

    // A header whose unpacked size is not the sum of its files' is refused.
    auto wrong_size = dia;
    wrong_size[0x1F] ^= 0x01;
    MemoryTarget sized;
    EXPECT_TRUE(!ctgp_update_apply(wrong_size.data(), wrong_size.size(), sized, [](const std::string&) {}, error));

    // A file the target's check refuses is never stored.
    MemoryTarget strict;
    strict.refuse = true;
    EXPECT_TRUE(!ctgp_update_apply(dia.data(), dia.size(), strict, [](const std::string&) {}, error));
    EXPECT_TRUE(strict.stored.empty());

    // The streaming path reads only the header and the section.
    std::size_t bytes_read = 0;
    target.stored.clear();
    EXPECT_TRUE(ctgp_update_apply(
        [&](std::uint64_t offset, std::uint8_t* out, std::size_t length) {
            if (offset + length > dia.size()) return false;
            std::memcpy(out, dia.data() + offset, length);
            bytes_read += length;
            return true;
        },
        dia.size(), target, [](const std::string&) {}, CtgpUpdateFileDone(), error));
    EXPECT_TRUE(bytes_read <= dia.size() + kCtgpDiaPrefixBytes);
    std::vector<std::uint8_t> streamed;
    EXPECT_TRUE(ctgp_yaz0_decode_nintendo(target.stored.data(), target.stored.size(), streamed, error));
    EXPECT_EQ(streamed, bytes("helloABCDEFGGGGGG"));

    // The bar inside the file only moves forward and ends full.
    std::vector<double> parts;
    target.stored.clear();
    EXPECT_TRUE(ctgp_update_apply(
        [&](std::uint64_t offset, std::uint8_t* out, std::size_t length) {
            if (offset + length > dia.size()) return false;
            std::memcpy(out, dia.data() + offset, length);
            return true;
        },
        dia.size(), target, [](const std::string&) {}, CtgpUpdateFileDone(),
        [&](std::size_t index, std::size_t total, const std::string& path, double part) {
            EXPECT_EQ(index, std::size_t(0));
            EXPECT_EQ(total, std::size_t(1));
            EXPECT_TRUE(path == "/Race/Course/NEW.szs");
            parts.push_back(part);
        },
        error));
    EXPECT_TRUE(parts.size() >= 3);
    for (std::size_t i = 1; i < parts.size(); ++i) EXPECT_TRUE(parts[i] >= parts[i - 1]);
    EXPECT_TRUE(!parts.empty() && parts.front() == 0.0 && parts.back() == 1.0);
}

// Nintendo's Yaz0, built by hand: header, then a flag byte and its items.
std::vector<std::uint8_t> nintendo_yaz0(std::uint32_t unpacked, const std::vector<std::uint8_t>& body) {
    std::vector<std::uint8_t> out = bytes("Yaz0");
    put32(out, unpacked);
    put32(out, 0);
    put32(out, 0);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

void test_yaz0_nintendo_matches() {
    std::string error;
    std::vector<std::uint8_t> out;

    // Two literals, then a short match (first byte 0x20: length 4, distance 1)
    // that repeats the last byte.
    auto file = nintendo_yaz0(6, {0xC0, 'A', 'B', 0x20, 0x00});
    EXPECT_TRUE(ctgp_yaz0_decode_nintendo(file.data(), file.size(), out, error));
    EXPECT_EQ(out, bytes("ABBBBB"));

    // A long match: high nibble 0, then a third byte 0x02 gives 2 + 0x12 = 20
    // bytes at distance 2, then one more literal. Without the third-byte form
    // the decoder would read 0x02 as the next flag byte and lose its place.
    file = nintendo_yaz0(23, {0xD0, 'x', 'y', 0x00, 0x01, 0x02, 'z'});
    EXPECT_TRUE(ctgp_yaz0_decode_nintendo(file.data(), file.size(), out, error));
    EXPECT_EQ(out, bytes("xyxyxyxyxyxyxyxyxyxyxyz"));

    // The longest form: 0xFF + 0x12 = 273 bytes.
    file = nintendo_yaz0(274, {0x80, 'q', 0x00, 0x00, 0xFF});
    EXPECT_TRUE(ctgp_yaz0_decode_nintendo(file.data(), file.size(), out, error));
    EXPECT_EQ(out, std::vector<std::uint8_t>(274, 'q'));

    // A long match whose third byte is missing.
    file = nintendo_yaz0(30, {0x80, 'q', 0x00, 0x00});
    EXPECT_TRUE(!ctgp_yaz0_decode_nintendo(file.data(), file.size(), out, error));

    // A match that reaches back before the start of the file.
    file = nintendo_yaz0(5, {0x80, 'q', 0x20, 0x01});
    EXPECT_TRUE(!ctgp_yaz0_decode_nintendo(file.data(), file.size(), out, error));
    file = nintendo_yaz0(4, {0x00, 0x20, 0x00});
    EXPECT_TRUE(!ctgp_yaz0_decode_nintendo(file.data(), file.size(), out, error));
}

// The stream decoder gives the same bytes as the whole-file one, however
// the input is cut, and refuses what the whole-file one refuses.
bool stream_decode(const std::vector<std::uint8_t>& file, std::size_t piece, std::vector<std::uint8_t>& out,
                   std::string& error) {
    out.clear();
    CtgpYaz0StreamDecoder decoder([&](const std::uint8_t* d, std::size_t n) {
        out.insert(out.end(), d, d + n);
        return true;
    });
    for (std::size_t at = 0; at < file.size(); at += piece) {
        const std::size_t take = std::min(piece, file.size() - at);
        if (!decoder.feed(file.data() + at, take, error)) return false;
    }
    return decoder.finish(error);
}

void test_yaz0_stream_decoder() {
    std::vector<std::uint8_t> input;
    std::uint32_t seed = 12345;
    for (int i = 0; i < 200000; ++i) {
        seed = seed * 1103515245u + 12345u;
        // Runs and repeats so the packer makes short and long matches.
        if ((seed >> 20) % 5 == 0 && input.size() > 300) {
            const std::size_t back = 1 + (seed >> 8) % 280;
            const std::size_t len = 3 + (seed >> 4) % 290;
            for (std::size_t k = 0; k < len; ++k) input.push_back(input[input.size() - back]);
        } else {
            input.push_back(std::uint8_t(seed >> 24));
        }
    }
    std::vector<std::uint8_t> packed;
    std::string error;
    EXPECT_TRUE(ctgp_yaz0_encode_exact(input.data(), input.size(), packed, error));
    packed.insert(packed.end(), 13, 0);  // padding after the file is ignored
    for (std::size_t piece : {std::size_t(1), std::size_t(2), std::size_t(7), std::size_t(24), std::size_t(25),
                              std::size_t(4096), std::size_t(65536), packed.size()}) {
        std::vector<std::uint8_t> out;
        EXPECT_TRUE(stream_decode(packed, piece, out, error));
        EXPECT_TRUE(out == input);
    }

    // The hand-built cases, a byte at a time.
    const std::vector<std::pair<std::vector<std::uint8_t>, std::string>> good = {
        {nintendo_yaz0(6, {0xC0, 'A', 'B', 0x20, 0x00}), "ABBBBB"},
        {nintendo_yaz0(23, {0xD0, 'x', 'y', 0x00, 0x01, 0x02, 'z'}), "xyxyxyxyxyxyxyxyxyxyxyz"},
        {nintendo_yaz0(0, {}), ""},
    };
    for (const auto& [file, want] : good) {
        std::vector<std::uint8_t> out;
        EXPECT_TRUE(stream_decode(file, 1, out, error));
        EXPECT_EQ(out, bytes(want));
    }
    const std::vector<std::vector<std::uint8_t>> bad = {
        nintendo_yaz0(30, {0x80, 'q', 0x00, 0x00}),  // third byte missing
        nintendo_yaz0(5, {0x80, 'q', 0x20, 0x01}),   // before the start
        nintendo_yaz0(4, {0x00, 0x20, 0x00}),
        nintendo_yaz0(9, {0xFF, 'a', 'b'}),          // cut short
        bytes("Yaz1"),
        bytes("Yaz0"),
    };
    for (const auto& file : bad) {
        std::vector<std::uint8_t> out;
        for (std::size_t piece : {std::size_t(1), std::size_t(64)}) {
            EXPECT_TRUE(!stream_decode(file, piece, out, error));
        }
    }
}

void test_pieces(bool in_file) {
    CtgpPieces pieces;
    const std::string scratch = "ctgpupdate-pieces.tmp";
    std::string error;
    if (in_file) EXPECT_TRUE(pieces.use_file(scratch, error));
    std::vector<std::uint8_t> all;
    std::uint32_t seed = 7;
    for (std::size_t chunk : {std::size_t(1), std::size_t(65535), std::size_t(2), std::size_t(200000),
                              std::size_t(65536)}) {
        std::vector<std::uint8_t> part(chunk);
        for (auto& b : part) b = std::uint8_t((seed = seed * 69069u + 1u) >> 24);
        EXPECT_TRUE(pieces.append(part.data(), part.size()));
        all.insert(all.end(), part.begin(), part.end());
    }
    EXPECT_EQ(pieces.size(), all.size());
    std::vector<std::uint8_t> back(all.size());
    EXPECT_TRUE(pieces.read(0, back.data(), back.size()));
    EXPECT_TRUE(back == all);
    std::vector<std::uint8_t> middle(70000);
    EXPECT_TRUE(pieces.read(65530, middle.data(), middle.size()));
    EXPECT_TRUE(std::equal(middle.begin(), middle.end(), all.begin() + 65530));
    EXPECT_TRUE(!pieces.read(all.size() - 1, middle.data(), 2));
    pieces.clear();
    EXPECT_EQ(pieces.size(), std::size_t(0));
    // The scratch file goes with clear().
    EXPECT_TRUE(std::fopen(scratch.c_str(), "rb") == nullptr);
}

void test_apply_rejects_bad_stream() {
    std::vector<std::uint8_t> stream = {0x55};  // no such opcode
    auto section = make_section(1, stream, "NEW.SZS");
    CtgpDif dif;
    std::string error;
    EXPECT_TRUE(ctgp_dif_parse(section.data(), section.size(), dif, error));
    MemoryBase base("ABCDEFG");
    std::vector<const CtgpBaseSource*> bases = {&base};
    std::vector<std::uint8_t> output;
    EXPECT_TRUE(!ctgp_dif_apply(section.data(), section.size(), dif, bases, collect, &output, error));

    // A base copy past the end of the base file is refused.
    stream = {0x14, 200};
    section = make_section(207, stream, "NEW.SZS");
    EXPECT_TRUE(ctgp_dif_parse(section.data(), section.size(), dif, error));
    output.clear();
    EXPECT_TRUE(!ctgp_dif_apply(section.data(), section.size(), dif, bases, collect, &output, error));
}

}  // namespace

int main() {
    test_yaz0_round_trip();
    test_dia_parse();
    test_apply();
    test_apply_distance();
    test_apply_full_window();
    test_dia_signature();
    test_apply_failure_kind();
    test_update_apply();
    test_apply_rejects_bad_stream();
    test_yaz0_nintendo_matches();
    test_yaz0_stream_decoder();
    test_pieces(false);
    test_pieces(true);
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "ctgpupdate: all tests passed" << std::endl;
    return 0;
}
