// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rtzstd.h"
#include "rtzstd_cases.hpp"
#include "riftwii/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <random>

namespace {

using rtzstd_test::Bytes;
using rtzstd_test::Frame;
int failures = 0;
std::uint64_t observations[5][5]{};
std::uint64_t checkpoints = 0;
alignas(8) std::array<std::uint8_t, RTZSTD_WORKSPACE_BYTES> workspace{};

void expect(bool condition, const std::string& label) {
    if (!condition) {
        if (failures < 30) std::cerr << "FAILED: " << label << '\n';
        ++failures;
    }
}

std::int32_t decode(const Bytes& input, Bytes& output, std::size_t capacity) {
    // Empty buffers still have a valid pointer, as required by the runtime API.
    return rtzstd_decode(workspace.data(), output.data(), static_cast<std::uint32_t>(capacity),
                          input.data(), static_cast<std::uint32_t>(input.size()));
}

std::string digest(const Bytes& data) {
    std::ostringstream hex;
    for (auto byte : riftwii::sha256(data.data(), data.size())) {
        hex << std::hex << std::setfill('0') << std::setw(2) << unsigned(byte);
    }
    return hex.str();
}

void valid_frame(const Frame& frame) {
    const auto size = frame.decoded.size();
    Bytes output(std::max<std::size_t>(size, 1));
    const auto result = decode(frame.compressed, output, size);
    expect(result == static_cast<std::int32_t>(size), frame.name + ": decoded size");
    expect(std::equal(frame.decoded.begin(), frame.decoded.end(), output.begin()), frame.name + ": bytes");
    if (!frame.sha256.empty()) expect(digest(frame.decoded) == frame.sha256, frame.name + ": original hash");
    // Output tail staging must also work with spare room and an unaligned
    // output base, which RVZ's exception-list prefixes can create.
    Bytes spare(size + 258, 0xA7);
    const auto roomy = rtzstd_decode(workspace.data(), spare.data() + 1,
                                     static_cast<std::uint32_t>(size + 256), frame.compressed.data(),
                                     static_cast<std::uint32_t>(frame.compressed.size()));
    expect(roomy == result && std::equal(frame.decoded.begin(), frame.decoded.end(), spare.begin() + 1),
           frame.name + ": spare capacity");
    expect(spare.front() == 0xA7 && spare.back() == 0xA7, frame.name + ": guards");
    if (size != 0) {
        Bytes small(std::max<std::size_t>(1, size - 1));
        expect(decode(frame.compressed, small, size - 1) < 0, frame.name + ": short destination");
    }
}

void damaged_frames(const Frame& frame, std::mt19937& random) {
    const auto bytes = frame.compressed.size();
    Bytes output(std::max<std::size_t>(frame.decoded.size(), 1));
    const std::size_t step = bytes <= 2048 ? 1 : std::max<std::size_t>(1, bytes / 96);
    for (std::size_t cut = 0; cut < bytes; cut += step) {
        Bytes truncated(frame.compressed.begin(), frame.compressed.begin() + cut);
        expect(decode(truncated, output, frame.decoded.size()) < 0, frame.name + ": truncation " + std::to_string(cut));
    }
    Bytes truncated(frame.compressed.begin(), frame.compressed.end() - 1);
    expect(decode(truncated, output, frame.decoded.size()) < 0, frame.name + ": missing last byte");
    for (unsigned trial = 0; trial < 48; ++trial) {
        Bytes mutated = frame.compressed;
        const auto at = random() % bytes;
        mutated[at] ^= static_cast<std::uint8_t>(1u << (random() % 8));
        const auto result = decode(mutated, output, frame.decoded.size());
        // Some mutations produce another valid frame, especially without a
        // checksum. Every accepted mutation must agree with the oracle.
        if (result >= 0) {
            Bytes reference(output.size());
            const auto got = ZSTD_decompress(reference.data(), frame.decoded.size(), mutated.data(), mutated.size());
            expect(!ZSTD_isError(got) && got == static_cast<std::size_t>(result) &&
                   std::equal(reference.begin(), reference.begin() + result, output.begin()),
                   frame.name + ": accepted mutation " + std::to_string(at));
        }
    }
    Bytes extra = frame.compressed;
    extra.push_back(0);
    expect(decode(extra, output, frame.decoded.size()) < 0, frame.name + ": trailing bytes");
}

Bytes raw_frame(const Bytes& payload, unsigned type = 0) {
    Bytes frame{0x28, 0xB5, 0x2F, 0xFD, 0xA0};
    for (unsigned i = 0; i < 4; ++i) frame.push_back(static_cast<std::uint8_t>(payload.size() >> (8 * i)));
    const auto header = (static_cast<std::uint32_t>(payload.size()) << 3) | (type << 1) | 1;
    for (unsigned i = 0; i < 3; ++i) frame.push_back(static_cast<std::uint8_t>(header >> (8 * i)));
    if (type == 1) frame.push_back(payload.empty() ? 0 : payload[0]);
    else frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

void directed_frames() {
    for (unsigned type = 0; type <= 1; ++type) {
        Frame frame{"handwritten-" + std::to_string(type), raw_frame(Bytes(32768, 0x6D), type), Bytes(32768, 0x6D), {}};
        valid_frame(frame);
    }
    Bytes output(32);
    auto frame = raw_frame(Bytes(16, 42));
    for (unsigned flag : {1u, 2u, 3u, 8u}) {
        auto bad = frame;
        bad[4] |= static_cast<std::uint8_t>(flag);
        expect(decode(bad, output, output.size()) < 0, "dictionary/reserved descriptor");
    }
    frame[9] |= 6;
    expect(decode(frame, output, output.size()) < 0, "reserved block type");
    expect(rtzstd_init(workspace.data(), workspace.size()) != nullptr, "workspace fits");
    expect(rtzstd_init(workspace.data(), workspace.size() - 1) == nullptr, "short workspace");
    expect(rtzstd_init(workspace.data() + 1, workspace.size()) == nullptr, "unaligned workspace");
    expect(rtzstd_init(nullptr, workspace.size()) == nullptr, "null workspace");
}

void random_inputs(std::mt19937& random) {
    for (unsigned trial = 0; trial < 20000; ++trial) {
        Bytes input(random() % 1024);
        for (auto& byte : input) byte = static_cast<std::uint8_t>(random());
        // Half are structured garbage with a plausible frame header, so
        // fuzzing reaches block parsing rather than just the magic check.
        if (input.size() >= 12 && (trial & 1)) {
            const Bytes header{0x28, 0xB5, 0x2F, 0xFD, 0, 0x28};
            std::copy(header.begin(), header.end(), input.begin());
            input[6] = static_cast<std::uint8_t>((input[6] & 7) | 5);
            input[7] = static_cast<std::uint8_t>((input.size() - 9) >> 5);
            input[8] = 0;
        }
        Bytes output(4096);
        const auto result = decode(input, output, output.size());
        if (result >= 0) {
            Bytes reference(output.size());
            const auto got = ZSTD_decompress(reference.data(), reference.size(), input.data(), input.size());
            expect(!ZSTD_isError(got) && got == static_cast<std::size_t>(result) &&
                   std::equal(reference.begin(), reference.begin() + result, output.begin()), "accepted fuzz input");
        }
    }
}

} // namespace

extern "C" void rtzstd_test_observe(unsigned kind, unsigned value) {
    if (kind < 5 && value < 5) ++observations[kind][value];
}

extern "C" void rtzstd_test_checkpoint() {
    ++checkpoints;
}

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    std::vector<Frame> frames;
    if (!rtzstd_test::load_frames(argv[1], frames)) {
        std::cerr << "Cannot load the Zstandard/RVZ corpus\n";
        return 1;
    }
    std::mt19937 random(8878);
    directed_frames();
    for (const auto& frame : frames) valid_frame(frame);
    for (unsigned kind = 0; kind < 5; ++kind) {
        std::cout << "Coverage " << kind << ':';
        for (auto count : observations[kind]) std::cout << ' ' << count;
        std::cout << '\n';
    }
    std::cout << "Valid frames: " << frames.size() << "; worker checkpoints: " << checkpoints << '\n';
    for (const auto& frame : frames) damaged_frames(frame, random);
    random_inputs(random);
    std::cout << "Failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
