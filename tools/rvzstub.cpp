// SPDX-License-Identifier: GPL-3.0-or-later
// Host tool: prints what RiftWii makes of an RVZ and, given an output
// path, writes the disc d2x would see for it (riftwii/rvz.hpp's stub at
// its disc offsets, zeros elsewhere, as large as the disc). Dolphin boots
// that file as the disc when the loader is told to read the partitions
// from the RVZ (autorun `rvz`), which checks the RVZ path without d2x.
//   rvzstub <game.rvz> [stub.iso]
#include "riftwii/rvz.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace riftwii;

namespace {

class StreamSource final : public ByteSource {
public:
    explicit StreamSource(const char* path) : in_(path, std::ios::binary) {
        in_.seekg(0, std::ios::end);
        size_ = in_ ? static_cast<std::uint64_t>(in_.tellg()) : 0;
    }
    bool ok() const { return static_cast<bool>(in_); }
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        in_.clear();
        in_.seekg(static_cast<std::streamoff>(offset));
        in_.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(length));
        return static_cast<std::size_t>(in_.gcount()) == length;
    }

private:
    mutable std::ifstream in_;
    std::uint64_t size_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: rvzstub <game.rvz> [stub.iso]\n";
        return 2;
    }
    auto file = std::make_shared<StreamSource>(argv[1]);
    if (!file->ok()) {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 1;
    }
    RvzHead head;
    std::string error;
    if (!read_rvz_head(*file, head, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    const RvzVerdict verdict = check_rvz(head, file->size());
    std::cout << describe_rvz(head) << ": "
              << (verdict.support == RvzSupport::Supported   ? "plays"
                  : verdict.support == RvzSupport::AtOwnRisk ? "plays at your own risk"
                                                             : "refused")
              << "\n";
    for (const std::string& r : verdict.reasons) std::cout << "  " << r << "\n";
    if (argc == 2) return 0;
    std::unique_ptr<RvzImage> image;
    RvzStub stub;
    if (!RvzImage::open(file, image, error) || !build_rvz_stub(*image, stub, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
    for (const RvzStubRange& r : stub.ranges) {
        out.seekp(static_cast<std::streamoff>(r.disc_offset));
        out.write(reinterpret_cast<const char*>(stub.bytes.data() + r.stub_offset), static_cast<std::streamsize>(r.length));
        std::cout << "  disc 0x" << std::hex << r.disc_offset << " +0x" << r.length << std::dec << "\n";
    }
    // As large as the disc, so its size reads as the original's.
    out.seekp(static_cast<std::streamoff>(head.iso_size - 1));
    out.put('\0');
    out.close();
    if (!out) {
        std::cerr << "cannot write " << argv[2] << "\n";
        return 1;
    }
    std::cout << "wrote " << argv[2] << " (" << head.iso_size << " bytes, " << stub.bytes.size() << " from the RVZ)\n";
    return 0;
}
