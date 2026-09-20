// SPDX-License-Identifier: GPL-3.0-or-later
#include "rtfs.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "fat32_image.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(c) do { if (!(c)) { std::cerr << "FAILED: " #c " at " << __LINE__ << std::endl; ++g_failures; } } while (0)
#define EXPECT_EQ(a, b) do { const auto a_ = (a); const auto b_ = (b); if (a_ != b_) { std::cerr << "FAILED: " #a " == " #b " at " << __LINE__ << " (" << a_ << " != " << b_ << ')' << std::endl; ++g_failures; } } while (0)

static_assert(sizeof(rtfs_iovec) == 8, "IOS vector layout");
static_assert(offsetof(rtfs_ipc, args) == 12, "IOS args start at 0x0c");
static_assert(offsetof(rtfs_ipc, callback) == 32, "IOS callback follows five args");
static_assert(offsetof(rtfs_ipc, user_data) == 36, "IOS user data layout");
static_assert(sizeof(rtfs_ipc) == 40, "active IOS request layout");
static_assert(offsetof(rtfs_attr_block, filepath) == 6, "ISFS attr filepath layout");
static_assert(offsetof(rtfs_attr_block, ownerperm) == 70, "ISFS attr permissions layout");
static_assert(sizeof(rtfs_attr_block) == 76, "ISFS attr payload layout");

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
static std::uint8_t* LowBuffer(std::size_t bytes) {
    for (std::uintptr_t h = 0x10000000; h < 0x70000000; h += 0x10000000) {
        void* p = VirtualAlloc(reinterpret_cast<void*>(h), bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p) return static_cast<std::uint8_t*>(p);
    }
    return nullptr;
}
#elif defined(__linux__)
#include <sys/mman.h>
static std::uint8_t* LowBuffer(std::size_t bytes) {
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    return p == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(p);
}
#else
static std::uint8_t* LowBuffer(std::size_t) { return nullptr; }
#endif

namespace {
std::uint32_t Addr(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

struct Low {
    std::uint8_t* base = nullptr;
    rtfs_request* request = nullptr;
    rtfs_iovec* vec = nullptr;
    rtfs_attr_block* attr = nullptr;
    std::uint8_t* bounce = nullptr;
    std::uint8_t* data = nullptr;
    static constexpr std::size_t kBounce = 2048;
    static constexpr std::size_t kData = 8192;
    Low() {
        base = LowBuffer(sizeof(rtfs_request) + sizeof(rtfs_iovec) * 4 + sizeof(rtfs_attr_block) + kBounce + kData + 128);
        if (!base) return;
        std::uintptr_t p = (reinterpret_cast<std::uintptr_t>(base) + 31) & ~std::uintptr_t(31);
        request = reinterpret_cast<rtfs_request*>(p);
        p = (p + sizeof(rtfs_request) + 31) & ~std::uintptr_t(31);
        vec = reinterpret_cast<rtfs_iovec*>(p);
        p += sizeof(rtfs_iovec) * 4;
        attr = reinterpret_cast<rtfs_attr_block*>(p);
        p += sizeof(rtfs_attr_block);
        p = (p + 31) & ~std::uintptr_t(31);
        bounce = reinterpret_cast<std::uint8_t*>(p);
        data = bounce + kBounce;
    }
};

struct Device {
    fatimg::Image& image;
    std::uint32_t transfers = 0;
    std::uint32_t fail_at = 0;
    explicit Device(fatimg::Image& i) : image(i) {}
    int transfer(const rtfat_op& op) {
        ++transfers;
        if (fail_at == transfers) return -1;
        const std::uint64_t at = std::uint64_t(op.io_lba) * 512;
        const std::uint64_t bytes = std::uint64_t(op.io_count) * 512;
        if (op.io_count == 0 || at + bytes > image.bytes.size()) return -2;
        auto* p = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(op.io_buffer));
        if (op.io_write) std::memcpy(image.bytes.data() + at, p, static_cast<std::size_t>(bytes));
        else std::memcpy(p, image.bytes.data() + at, static_cast<std::size_t>(bytes));
        return 0;
    }
};

struct Fixture {
    fatimg::Image image{512, 2, 0};
    Device dev{image};
    rtfat_volume volume{};
    rtfs_context fs{};
    std::string prefix = "/title/00010000/524d4345/data";
    Fixture() {
        const auto bytes = image.cluster_bytes();
        fatimg::Bytes save;
        fatimg::Bytes content(bytes + 37);
        for (std::size_t i = 0; i < content.size(); ++i) content[i] = static_cast<std::uint8_t>(i * 7 + 3);
        image.write_data({10, 11}, content);
        fatimg::Bytes banner = fatimg::short_entry("BANNER  BIN", 0x20, 10, static_cast<std::uint32_t>(content.size()), 0x18);
        save.insert(save.end(), banner.begin(), banner.end());
        fatimg::Bytes other = fatimg::short_entry("OTHER   DAT", 0x20, 12, 0);
        save.insert(save.end(), other.begin(), other.end());
        EXPECT_TRUE(image.write_dir({3}, save));
        volume.sectors_per_cluster = image.spc;
        volume.fat_lba = image.reserved;
        volume.fat_count = image.fats;
        volume.fat_sectors = image.fat_sectors;
        volume.data_lba = static_cast<std::uint32_t>(image.data_start_sector());
        volume.cluster_count = image.clusters;
        volume.dir_cluster = 3;
        volume.alloc_hint = 13;
        EXPECT_EQ(rtfs_init(&fs, &volume, prefix.c_str(), 21), RTFAT_OK);
    }
};

void CopyPath(std::uint8_t* out, const std::string& path) {
    std::memset(out, 0, RTFS_PATH_BYTES);
    std::memcpy(out, path.c_str(), path.size());
}

int Run(Fixture& fx, Low& low, const rtfs_ipc& ipc) {
    std::memset(low.request, 0, sizeof(*low.request));
    low.request->fat.bounce = Addr(low.bounce);
    low.request->fat.bounce_bytes = Low::kBounce;
    rtfs_begin(&fx.fs, low.request, &ipc);
    for (int guard = 0; guard < 10000 && low.request->classification == RTFS_NEEDS_IO; ++guard) {
        const int r = rtfs_step(&fx.fs, low.request);
        if (r == RTFAT_IO) low.request->fat.io_status = fx.dev.transfer(low.request->fat);
    }
    return low.request->result;
}

rtfs_ipc Open(std::uint32_t path, std::uint32_t mode, std::uint32_t callback = 0, std::uint32_t user_data = 0) {
    rtfs_ipc ipc{}; ipc.command = RTFS_CMD_OPEN; ipc.args.open.path = path; ipc.args.open.mode = mode; ipc.callback = callback; ipc.user_data = user_data; return ipc;
}

void TestOpenReadSeekAndPass(Low& low) {
    Fixture fx;
    CopyPath(low.data, fx.prefix + "/banner.bin");
    const int fd = Run(fx, low, Open(Addr(low.data), 3, 0x12345678, 0x87654321));
    EXPECT_TRUE(fd >= static_cast<int>(RTFS_FD_BASE));
    EXPECT_EQ(low.request->callback, 0x12345678u);
    EXPECT_EQ(low.request->user_data, 0x87654321u);
    rtfs_ipc read{}; read.command = RTFS_CMD_READ; read.fd = fd; read.args.readwrite.data = Addr(low.data + 128); read.args.readwrite.length = 17;
    EXPECT_EQ(Run(fx, low, read), 17);
    EXPECT_EQ(low.data[128], 3u);
    EXPECT_EQ(low.data[144], static_cast<std::uint8_t>(16 * 7 + 3));
    rtfs_ipc seek{}; seek.command = RTFS_CMD_SEEK; seek.fd = fd; seek.args.seek.where = -7; seek.args.seek.whence = RTFS_SEEK_END;
    EXPECT_EQ(Run(fx, low, seek), 1054);
    seek.args.seek.where = -2000; seek.args.seek.whence = RTFS_SEEK_SET;
    EXPECT_EQ(Run(fx, low, seek), RTFAT_EINVAL);
    seek.args.seek.where = 1062; seek.args.seek.whence = RTFS_SEEK_SET;  // one past the end
    EXPECT_EQ(Run(fx, low, seek), RTFAT_EINVAL);
    seek.args.seek.where = 1061;
    EXPECT_EQ(Run(fx, low, seek), 1061);
    seek.args.seek.where = 1; seek.args.seek.whence = RTFS_SEEK_END;
    EXPECT_EQ(Run(fx, low, seek), RTFAT_EINVAL);
    const std::uint32_t slot = (static_cast<std::uint32_t>(fd) - RTFS_FD_BASE) & (RTFS_MAX_FDS - 1u);
    const std::uint32_t saved_position = fx.fs.files[slot].fat.position;
    seek.args.seek.where = (-2147483647 - 1); seek.args.seek.whence = RTFS_SEEK_SET;
    EXPECT_EQ(Run(fx, low, seek), RTFAT_EINVAL); EXPECT_EQ(fx.fs.files[slot].fat.position, saved_position);
    fx.fs.files[slot].fat.position = 0x7fffffffu;
    seek.args.seek.where = 1; seek.args.seek.whence = RTFS_SEEK_CUR;
    EXPECT_EQ(Run(fx, low, seek), RTFAT_EINVAL); EXPECT_EQ(fx.fs.files[slot].fat.position, 0x7fffffffu);
    fx.fs.files[slot].fat.size = 0x80000000u;
    seek.args.seek.where = 0; seek.args.seek.whence = RTFS_SEEK_END;
    EXPECT_EQ(Run(fx, low, seek), RTFAT_EINVAL); EXPECT_EQ(fx.fs.files[slot].fat.position, 0x7fffffffu);
    CopyPath(low.data, "/title/00010000/524d4345/other/banner.bin");
    rtfs_ipc pass = Open(Addr(low.data), 1);
    rtfs_begin(&fx.fs, low.request, &pass);
    EXPECT_EQ(low.request->classification, RTFS_PASS_THROUGH);
    CopyPath(low.data, fx.prefix + "/a/b");
    EXPECT_EQ(Run(fx, low, Open(Addr(low.data), 1)), RTFAT_EINVAL);
}

void TestModesStaleAndBusy(Low& low) {
    Fixture fx;
    CopyPath(low.data, fx.prefix + "/banner.bin");
    const int read_fd = Run(fx, low, Open(Addr(low.data), 1));
    rtfs_ipc write{}; write.command = RTFS_CMD_WRITE; write.fd = read_fd; write.args.readwrite.data = Addr(low.data + 128); write.args.readwrite.length = 1;
    EXPECT_EQ(Run(fx, low, write), RTFAT_EACCESS);
    rtfs_ipc fake_ioctlv{}; fake_ioctlv.command = RTFS_CMD_IOCTLV; fake_ioctlv.fd = read_fd; fake_ioctlv.args.ioctlv.request = RTFS_IOCTL_READDIR;
    EXPECT_EQ(Run(fx, low, fake_ioctlv), RTFAT_EINVAL);
    rtfs_ipc close{}; close.command = RTFS_CMD_CLOSE; close.fd = read_fd;
    EXPECT_EQ(Run(fx, low, close), RTFAT_OK);
    rtfs_begin(&fx.fs, low.request, &close);
    EXPECT_EQ(low.request->result, RTFAT_EINVAL);
    fake_ioctlv.fd = read_fd;
    EXPECT_EQ(Run(fx, low, fake_ioctlv), RTFAT_EINVAL);
    close.fd = static_cast<std::int32_t>(RTFS_FD_BASE + 999u);
    EXPECT_EQ(Run(fx, low, close), RTFAT_EINVAL);
    int fds[RTFS_MAX_FDS]{};
    for (std::uint32_t i = 0; i < RTFS_MAX_FDS; ++i) fds[i] = Run(fx, low, Open(Addr(low.data), 1));
    EXPECT_EQ(Run(fx, low, Open(Addr(low.data), 1)), RTFAT_ENOINODES);
    for (std::uint32_t i = 0; i < RTFS_MAX_FDS; ++i) { close.fd = fds[i]; EXPECT_EQ(Run(fx, low, close), RTFAT_OK); }
    std::memset(low.request, 0, sizeof(*low.request)); low.request->fat.bounce = Addr(low.bounce); low.request->fat.bounce_bytes = Low::kBounce;
    rtfs_ipc first = Open(Addr(low.data), 1);
    rtfs_begin(&fx.fs, low.request, &first);
    EXPECT_EQ(low.request->classification, RTFS_NEEDS_IO);
    rtfs_request second{}; second.fat.bounce = Addr(low.bounce); second.fat.bounce_bytes = Low::kBounce;
    rtfs_ipc next = Open(Addr(low.data), 1);
    rtfs_begin(&fx.fs, &second, &next);
    EXPECT_EQ(second.classification, RTFS_COMPLETE);
    EXPECT_EQ(second.result, RTFAT_EACCESS);
    while (rtfs_step(&fx.fs, low.request) == RTFAT_IO) low.request->fat.io_status = fx.dev.transfer(low.request->fat);
}

void TestWritePersists(Low& low) {
    Fixture fx;
    CopyPath(low.data, fx.prefix + "/banner.bin");
    const int fd = Run(fx, low, Open(Addr(low.data), 3));
    rtfs_ipc seek{}; seek.command = RTFS_CMD_SEEK; seek.fd = fd; seek.args.seek.where = 40; seek.args.seek.whence = RTFS_SEEK_SET;
    EXPECT_EQ(Run(fx, low, seek), 40);
    const std::uint8_t expected[] = {0x91, 0x27, 0x6C, 0xD4, 0x08, 0xFE};
    std::memcpy(low.data + 128, expected, sizeof(expected));
    rtfs_ipc write{}; write.command = RTFS_CMD_WRITE; write.fd = fd; write.args.readwrite.data = Addr(low.data + 128);
    write.args.readwrite.length = sizeof(expected);
    EXPECT_EQ(Run(fx, low, write), static_cast<int>(sizeof(expected)));
    const std::uint32_t slot = (static_cast<std::uint32_t>(fd) - RTFS_FD_BASE) & (RTFS_MAX_FDS - 1u);
    EXPECT_EQ(fx.fs.files[slot].fat.position, 46u);
    seek.args.seek.where = 40;
    EXPECT_EQ(Run(fx, low, seek), 40);
    rtfs_ipc read{}; read.command = RTFS_CMD_READ; read.fd = fd; read.args.readwrite.data = Addr(low.data + 256);
    read.args.readwrite.length = sizeof(expected);
    EXPECT_EQ(Run(fx, low, read), static_cast<int>(sizeof(expected)));
    EXPECT_EQ(std::memcmp(low.data + 256, expected, sizeof(expected)), 0);
    EXPECT_EQ(fx.fs.files[slot].fat.position, 46u);
}

void TestIoctlsAndDirectory(Low& low) {
    Fixture fx;
    std::memset(low.attr, 0, sizeof(*low.attr));
    CopyPath(reinterpret_cast<std::uint8_t*>(low.attr->filepath), fx.prefix + "/new.dat");
    low.attr->ownerperm = 3; low.attr->groupperm = 3; low.attr->otherperm = 3;
    rtfs_ipc create{}; create.command = RTFS_CMD_IOCTL; create.fd = 21; create.args.ioctl.request = RTFS_IOCTL_CREATEFILE;
    create.args.ioctl.in = Addr(low.attr); create.args.ioctl.in_len = sizeof(*low.attr);
    EXPECT_EQ(Run(fx, low, create), RTFAT_OK);
    rtfs_ipc attr{}; attr.command = RTFS_CMD_IOCTL; attr.fd = 21; attr.args.ioctl.request = RTFS_IOCTL_GETATTR;
    CopyPath(low.data, fx.prefix + "/new.dat"); attr.args.ioctl.in = Addr(low.data); attr.args.ioctl.in_len = RTFS_PATH_BYTES;
    attr.args.ioctl.out = Addr(low.attr); attr.args.ioctl.out_len = sizeof(*low.attr);
    EXPECT_EQ(Run(fx, low, attr), RTFAT_OK);
    EXPECT_EQ(low.attr->owner_id, RTFS_META_OWNER_ID); EXPECT_EQ(low.attr->group_id, RTFS_META_GROUP_ID);
    EXPECT_EQ(low.attr->attributes, RTFS_META_ATTRIBUTES); EXPECT_EQ(low.attr->ownerperm, RTFS_META_OWNER_PERM);
    EXPECT_EQ(low.attr->groupperm, RTFS_META_GROUP_PERM); EXPECT_EQ(low.attr->otherperm, RTFS_META_OTHER_PERM);
    attr.args.ioctl.request = RTFS_IOCTL_SETATTR; attr.args.ioctl.in = Addr(low.attr); attr.args.ioctl.in_len = sizeof(*low.attr);
    EXPECT_EQ(Run(fx, low, attr), RTFAT_OK);
    rtfs_ipc dir{}; dir.command = RTFS_CMD_IOCTL; dir.fd = 21; dir.args.ioctl.request = RTFS_IOCTL_CREATEDIR;
    std::memset(low.attr, 0, sizeof(*low.attr)); CopyPath(reinterpret_cast<std::uint8_t*>(low.attr->filepath), fx.prefix);
    dir.args.ioctl.in = Addr(low.attr); dir.args.ioctl.in_len = sizeof(*low.attr);
    EXPECT_EQ(Run(fx, low, dir), RTFAT_EEXIST);
    CopyPath(reinterpret_cast<std::uint8_t*>(low.attr->filepath), fx.prefix + "/subdir");
    EXPECT_EQ(Run(fx, low, dir), RTFAT_EACCESS);
    CopyPath(low.data, fx.prefix);
    std::uint32_t* count = reinterpret_cast<std::uint32_t*>(low.data + 128); *count = 8;
    low.vec[0] = {Addr(low.data), RTFS_PATH_BYTES}; low.vec[1] = {Addr(count), 4}; low.vec[2] = {Addr(low.data + 256), 8 * RTFAT_SLOT_BYTES}; low.vec[3] = {Addr(count), 4};
    rtfs_ipc list{}; list.command = RTFS_CMD_IOCTLV; list.fd = 21; list.args.ioctlv.request = RTFS_IOCTL_READDIR;
    list.args.ioctlv.in_count = 2; list.args.ioctlv.out_count = 2; list.args.ioctlv.vectors = Addr(low.vec);
    EXPECT_EQ(Run(fx, low, list), RTFAT_OK); EXPECT_EQ(*count, 3u);
    EXPECT_TRUE(std::string(reinterpret_cast<char*>(low.data + 256)) == "banner.bin");
    EXPECT_TRUE(std::string(reinterpret_cast<char*>(low.data + 256 + 11)).size() > 0);  // the next name follows the NUL, as IOS packs them
    fx.dev.fail_at = fx.dev.transfers + 1;
    EXPECT_EQ(Run(fx, low, list), RTFAT_EIO);
    std::uint32_t* blocks = reinterpret_cast<std::uint32_t*>(low.data + 512); std::uint32_t* files = blocks + 1;
    low.vec[0] = {Addr(low.data), RTFS_PATH_BYTES}; low.vec[1] = {Addr(blocks), 4}; low.vec[2] = {Addr(files), 4};
    rtfs_ipc usage{}; usage.command = RTFS_CMD_IOCTLV; usage.fd = 21; usage.args.ioctlv.request = RTFS_IOCTL_GETUSAGE;
    usage.args.ioctlv.in_count = 1; usage.args.ioctlv.out_count = 2; usage.args.ioctlv.vectors = Addr(low.vec);
    EXPECT_EQ(Run(fx, low, usage), RTFAT_OK); EXPECT_EQ(*files, 3u); EXPECT_TRUE(*blocks >= 1u);
    fx.dev.fail_at = fx.dev.transfers + 1;
    EXPECT_EQ(Run(fx, low, usage), RTFAT_EIO);
}

void TestStatsRenameDeleteAndFailure(Low& low) {
    Fixture fx;
    CopyPath(low.data, fx.prefix + "/banner.bin");
    const int fd = Run(fx, low, Open(Addr(low.data), 3));
    auto* stats = reinterpret_cast<std::uint32_t*>(low.data + 128);
    rtfs_ipc stat{}; stat.command = RTFS_CMD_IOCTL; stat.fd = fd; stat.args.ioctl.request = RTFS_IOCTL_GETFILESTATS; stat.args.ioctl.out = Addr(stats); stat.args.ioctl.out_len = 8;
    EXPECT_EQ(Run(fx, low, stat), RTFAT_OK); EXPECT_EQ(stats[0], 1061u); EXPECT_EQ(stats[1], 0u);
    CopyPath(low.data, fx.prefix + "/other.dat"); CopyPath(low.data + RTFS_PATH_BYTES, fx.prefix + "/moved.dat");
    rtfs_ipc rename{}; rename.command = RTFS_CMD_IOCTL; rename.fd = 21; rename.args.ioctl.request = RTFS_IOCTL_RENAME;
    rename.args.ioctl.in = Addr(low.data); rename.args.ioctl.in_len = 2 * RTFS_PATH_BYTES;
    EXPECT_EQ(Run(fx, low, rename), RTFAT_OK);
    rtfs_ipc del{}; del.command = RTFS_CMD_IOCTL; del.fd = 21; del.args.ioctl.request = RTFS_IOCTL_DELETE; del.args.ioctl.in = Addr(low.data + RTFS_PATH_BYTES); del.args.ioctl.in_len = RTFS_PATH_BYTES;
    EXPECT_EQ(Run(fx, low, del), RTFAT_OK);
    // Renaming onto an existing file replaces it, as IOS does: banner.bin
    // (1061 bytes) becomes other.dat; the old other.dat is gone.
    {
        std::memset(low.attr, 0, sizeof(*low.attr));
        CopyPath(reinterpret_cast<std::uint8_t*>(low.attr->filepath), fx.prefix + "/other.dat");
        rtfs_ipc create{}; create.command = RTFS_CMD_IOCTL; create.fd = 21; create.args.ioctl.request = RTFS_IOCTL_CREATEFILE;
        create.args.ioctl.in = Addr(low.attr); create.args.ioctl.in_len = sizeof(*low.attr);
        EXPECT_EQ(Run(fx, low, create), RTFAT_OK);
        CopyPath(low.data, fx.prefix + "/banner.bin"); CopyPath(low.data + RTFS_PATH_BYTES, fx.prefix + "/other.dat");
        EXPECT_EQ(Run(fx, low, rename), RTFAT_OK);
        CopyPath(low.data, fx.prefix + "/other.dat");
        const int moved = Run(fx, low, Open(Addr(low.data), 1));
        EXPECT_TRUE(moved >= static_cast<int>(RTFS_FD_BASE));
        auto* size = reinterpret_cast<std::uint32_t*>(low.data + 128);
        rtfs_ipc st{}; st.command = RTFS_CMD_IOCTL; st.fd = moved; st.args.ioctl.request = RTFS_IOCTL_GETFILESTATS; st.args.ioctl.out = Addr(size); st.args.ioctl.out_len = 8;
        EXPECT_EQ(Run(fx, low, st), RTFAT_OK); EXPECT_EQ(size[0], 1061u);
        rtfs_ipc close{}; close.command = RTFS_CMD_CLOSE; close.fd = moved;
        EXPECT_EQ(Run(fx, low, close), RTFAT_OK);
        CopyPath(low.data, fx.prefix + "/banner.bin");
        EXPECT_EQ(Run(fx, low, Open(Addr(low.data), 1)), RTFAT_ENOENT);
        EXPECT_EQ(fx.fs.busy, 0u);
        // Put banner.bin back for the failure case below.
        CopyPath(low.data, fx.prefix + "/other.dat"); CopyPath(low.data + RTFS_PATH_BYTES, fx.prefix + "/banner.bin");
        EXPECT_EQ(Run(fx, low, rename), RTFAT_OK);
    }
    // The public classifier.
    {
        char name[RTFAT_NAME_MAX + 1];
        EXPECT_EQ(rtfs_path_type(&fx.fs, "/tmp/banner.bin", name), RTFS_PATH_OUTSIDE);
        EXPECT_EQ(rtfs_path_type(&fx.fs, fx.prefix.c_str(), name), RTFS_PATH_DIR);
        EXPECT_EQ(rtfs_path_type(&fx.fs, (fx.prefix + "/rksys.dat").c_str(), name), RTFS_PATH_FILE);
        EXPECT_TRUE(std::string(name) == "rksys.dat");
        EXPECT_EQ(rtfs_path_type(&fx.fs, (fx.prefix + "/a/b").c_str(), name), RTFS_PATH_BAD);
        EXPECT_EQ(rtfs_path_type(nullptr, "/x", name), RTFS_PATH_OUTSIDE);
    }
    CopyPath(low.data, fx.prefix + "/banner.bin");
    fx.dev.fail_at = fx.dev.transfers + 1;
    EXPECT_EQ(Run(fx, low, Open(Addr(low.data), 1)), RTFAT_EIO);
}

// rtfs_probe classifies like rtfs_begin and changes nothing; the /dev/fs
// fd is learned from its open, forgotten at its close, and unknown means
// any real fd carries ISFS requests.
void TestProbeAndFsFd(Low& low) {
    Fixture fx;
    EXPECT_TRUE(rtfs_is_fs_device("/dev/fs"));
    EXPECT_TRUE(!rtfs_is_fs_device("/dev/fs/"));
    EXPECT_TRUE(!rtfs_is_fs_device("/dev/f"));
    EXPECT_TRUE(!rtfs_is_fs_device(nullptr));
    EXPECT_EQ(fx.fs.fs_fd, 21);

    // A probe of an open: NEEDS_IO, but the engine stays idle.
    CopyPath(low.data, fx.prefix + "/banner.bin");
    rtfs_ipc open = Open(Addr(low.data), 3);
    std::memset(low.request, 0, sizeof(*low.request));
    rtfs_probe(&fx.fs, low.request, &open);
    EXPECT_EQ(low.request->classification, RTFS_NEEDS_IO);
    EXPECT_EQ(fx.fs.busy, 0u);
    EXPECT_EQ(low.request->active, 0u);
    EXPECT_EQ(rtfs_step(&fx.fs, low.request), RTFAT_DONE);  // nothing to drive
    // A probe while busy still says NEEDS_IO (the busy rule is the caller's).
    fx.fs.busy = 1;
    rtfs_probe(&fx.fs, low.request, &open);
    EXPECT_EQ(low.request->classification, RTFS_NEEDS_IO);
    fx.fs.busy = 0;
    const int fd = Run(fx, low, open);
    EXPECT_TRUE(fd >= static_cast<int>(RTFS_FD_BASE));
    const std::uint32_t slot = (static_cast<std::uint32_t>(fd) - RTFS_FD_BASE) & (RTFS_MAX_FDS - 1u);

    // Probed seek and close: the answers, without the effects.
    rtfs_ipc seek{}; seek.command = RTFS_CMD_SEEK; seek.fd = fd; seek.args.seek.where = 5; seek.args.seek.whence = RTFS_SEEK_SET;
    rtfs_probe(&fx.fs, low.request, &seek);
    EXPECT_EQ(low.request->classification, RTFS_COMPLETE);
    EXPECT_EQ(low.request->result, 5);
    EXPECT_EQ(fx.fs.files[slot].fat.position, 0u);
    auto* stats = reinterpret_cast<std::uint32_t*>(low.data + 128);
    stats[0] = 0xAAAAAAAAu; stats[1] = 0xBBBBBBBBu;
    rtfs_ipc stat{}; stat.command = RTFS_CMD_IOCTL; stat.fd = fd; stat.args.ioctl.request = RTFS_IOCTL_GETFILESTATS;
    stat.args.ioctl.out = Addr(stats); stat.args.ioctl.out_len = 8;
    rtfs_probe(&fx.fs, low.request, &stat);
    EXPECT_EQ(low.request->classification, RTFS_COMPLETE);
    EXPECT_EQ(low.request->result, RTFAT_OK);
    EXPECT_EQ(stats[0], 0xAAAAAAAAu);
    rtfs_ipc close{}; close.command = RTFS_CMD_CLOSE; close.fd = fd;
    rtfs_probe(&fx.fs, low.request, &close);
    EXPECT_EQ(low.request->classification, RTFS_COMPLETE);
    EXPECT_EQ(low.request->result, RTFAT_OK);
    EXPECT_EQ(fx.fs.files[slot].in_use, 1u);
    // A probe of a directory GetAttr leaves the block alone.
    std::memset(low.attr, 0x5A, sizeof(*low.attr));
    CopyPath(low.data, fx.prefix);
    rtfs_ipc attr{}; attr.command = RTFS_CMD_IOCTL; attr.fd = 21; attr.args.ioctl.request = RTFS_IOCTL_GETATTR;
    attr.args.ioctl.in = Addr(low.data); attr.args.ioctl.in_len = RTFS_PATH_BYTES;
    attr.args.ioctl.out = Addr(low.attr); attr.args.ioctl.out_len = sizeof(*low.attr);
    rtfs_probe(&fx.fs, low.request, &attr);
    EXPECT_EQ(low.request->classification, RTFS_COMPLETE);
    EXPECT_EQ(low.request->result, RTFAT_OK);
    EXPECT_EQ(low.attr->ownerperm, 0x5Au);
    // Pass-through classifies as such in a probe too.
    CopyPath(low.data, "/dev/fs");
    rtfs_ipc dev = Open(Addr(low.data), 0);
    rtfs_probe(&fx.fs, low.request, &dev);
    EXPECT_EQ(low.request->classification, RTFS_PASS_THROUGH);
    EXPECT_EQ(Run(fx, low, close), RTFAT_OK);

    // The fs fd: a request on another real fd passes while it is known...
    CopyPath(low.data, fx.prefix);
    attr.fd = 22;
    rtfs_begin(&fx.fs, low.request, &attr);
    EXPECT_EQ(low.request->classification, RTFS_PASS_THROUGH);
    // ...its close forgets it (and passes through)...
    rtfs_ipc close_fs{}; close_fs.command = RTFS_CMD_CLOSE; close_fs.fd = 21;
    rtfs_begin(&fx.fs, low.request, &close_fs);
    EXPECT_EQ(low.request->classification, RTFS_PASS_THROUGH);
    EXPECT_EQ(fx.fs.fs_fd, -1);
    // ...after which any real fd carries ISFS requests, but never a fake one.
    EXPECT_EQ(Run(fx, low, attr), RTFAT_OK);
    EXPECT_EQ(low.attr->ownerperm, RTFS_META_OWNER_PERM);
    // Learning takes a real fd only.
    rtfs_learn_fs_fd(&fx.fs, static_cast<std::int32_t>(RTFS_FD_BASE + 8u));
    EXPECT_EQ(fx.fs.fs_fd, -1);
    rtfs_learn_fs_fd(&fx.fs, -6);
    EXPECT_EQ(fx.fs.fs_fd, -1);
    rtfs_learn_fs_fd(&fx.fs, 9);
    EXPECT_EQ(fx.fs.fs_fd, 9);
    rtfs_begin(&fx.fs, low.request, &attr);  // fd 22 again: not the fs device now
    EXPECT_EQ(low.request->classification, RTFS_PASS_THROUGH);
    EXPECT_EQ(rtfs_init(&fx.fs, &fx.volume, fx.prefix.c_str(), -1), RTFAT_OK);
    EXPECT_EQ(fx.fs.fs_fd, -1);

    // With the fd unknown, the ISFS ioctl numbers also reach us from the
    // network devices (/dev/net/ip/top: 3 close, 5 fcntl, 7 getsockname,
    // 9 setsockopt, 4 connect, 6 getpeername, 8 getsockopt, 12 recvfrom
    // as an ioctlv) with small buffers. Nothing is ours until a path
    // under the prefix says so: every such request passes through
    // untouched, never answered -101 on IOS's behalf.
    std::uint32_t* word = reinterpret_cast<std::uint32_t*>(low.data + 1024);
    word[0] = 3; word[1] = 0; word[2] = 0; word[3] = 0;
    for (std::uint32_t code = 3; code <= 9; ++code) {
        rtfs_ipc sock{}; sock.command = RTFS_CMD_IOCTL; sock.fd = 4; sock.args.ioctl.request = code;
        sock.args.ioctl.in = Addr(word); sock.args.ioctl.in_len = 4; sock.args.ioctl.out = Addr(word + 1); sock.args.ioctl.out_len = 12;
        rtfs_begin(&fx.fs, low.request, &sock);
        EXPECT_EQ(low.request->classification, RTFS_PASS_THROUGH);
        sock.args.ioctl.in = 0; sock.args.ioctl.in_len = 0;
        rtfs_begin(&fx.fs, low.request, &sock);
        EXPECT_EQ(low.request->classification, RTFS_PASS_THROUGH);
    }
    // A 64-byte buffer that is not a path under the prefix: not ours either,
    // and neither is one that is unterminated within 64 bytes.
    std::memset(low.data, 'x', 64);
    rtfs_ipc del{}; del.command = RTFS_CMD_IOCTL; del.fd = 4; del.args.ioctl.request = RTFS_IOCTL_DELETE;
    del.args.ioctl.in = Addr(low.data); del.args.ioctl.in_len = RTFS_PATH_BYTES;
    rtfs_begin(&fx.fs, low.request, &del);
    EXPECT_EQ(low.request->classification, RTFS_PASS_THROUGH);
    std::memcpy(low.data, fx.prefix.c_str(), fx.prefix.size());  // the prefix, then 'x' to the end: unterminated
    rtfs_begin(&fx.fs, low.request, &del);
    EXPECT_EQ(low.request->classification, RTFS_PASS_THROUGH);
    // Vector-form: too few vectors, a null vector table, a wrong count shape.
    low.vec[0] = {Addr(word), 4}; low.vec[1] = {Addr(word + 1), 4};
    rtfs_ipc recv{}; recv.command = RTFS_CMD_IOCTLV; recv.fd = 4; recv.args.ioctlv.request = RTFS_IOCTL_READDIR;
    recv.args.ioctlv.in_count = 1; recv.args.ioctlv.out_count = 1; recv.args.ioctlv.vectors = Addr(low.vec);
    rtfs_begin(&fx.fs, low.request, &recv);
    EXPECT_EQ(low.request->classification, RTFS_PASS_THROUGH);
    recv.args.ioctlv.vectors = 0;
    rtfs_begin(&fx.fs, low.request, &recv);
    EXPECT_EQ(low.request->classification, RTFS_PASS_THROUGH);
    recv.args.ioctlv.request = RTFS_IOCTL_GETUSAGE; recv.args.ioctlv.in_count = 3; recv.args.ioctlv.vectors = Addr(low.vec);
    rtfs_begin(&fx.fs, low.request, &recv);
    EXPECT_EQ(low.request->classification, RTFS_PASS_THROUGH);
    // ReadDir and GetUsage on a file name (the SDK's existence probe): -101
    // for a file that exists, -106 for none, from a lookup.
    CopyPath(low.data, fx.prefix + "/banner.bin");
    low.vec[0] = {Addr(low.data), RTFS_PATH_BYTES}; low.vec[1] = {Addr(word), 4}; low.vec[2] = {Addr(word + 1), 4};
    recv.args.ioctlv.request = RTFS_IOCTL_READDIR; recv.args.ioctlv.in_count = 1; recv.args.ioctlv.out_count = 1;
    {
        const std::uint32_t before = fx.dev.transfers;
        EXPECT_EQ(Run(fx, low, recv), RTFAT_EINVAL);
        EXPECT_TRUE(fx.dev.transfers > before);
    }
    recv.args.ioctlv.request = RTFS_IOCTL_GETUSAGE; recv.args.ioctlv.out_count = 2;
    EXPECT_EQ(Run(fx, low, recv), RTFAT_EINVAL);
    CopyPath(low.data, fx.prefix + "/rksys.dat");
    EXPECT_EQ(Run(fx, low, recv), RTFAT_ENOENT);
    recv.args.ioctlv.request = RTFS_IOCTL_READDIR; recv.args.ioctlv.out_count = 1;
    EXPECT_EQ(Run(fx, low, recv), RTFAT_ENOENT);
    EXPECT_EQ(fx.fs.busy, 0u);

    // But once the path is ours, the rest of the arguments are checked.
    CopyPath(low.data, fx.prefix);
    low.vec[0] = {Addr(low.data), RTFS_PATH_BYTES}; low.vec[1] = {0, 0}; low.vec[2] = {0, 0};
    recv.args.ioctlv.request = RTFS_IOCTL_GETUSAGE; recv.args.ioctlv.in_count = 1; recv.args.ioctlv.out_count = 2;
    EXPECT_EQ(Run(fx, low, recv), RTFAT_EINVAL);
    recv.args.ioctlv.request = RTFS_IOCTL_READDIR; recv.args.ioctlv.out_count = 1;
    EXPECT_EQ(Run(fx, low, recv), RTFAT_EINVAL);
}
}  // namespace

int main() {
    Low low;
    if (!low.base) { std::cerr << "no memory below 4 GiB; skipping rtfs tests" << std::endl; return 0; }
    TestOpenReadSeekAndPass(low);
    TestModesStaleAndBusy(low);
    TestWritePersists(low);
    TestIoctlsAndDirectory(low);
    TestStatsRenameDeleteAndFailure(low);
    TestProbeAndFsFd(low);
    if (g_failures == 0) std::cout << "rtfs tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
