// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Pure SD write probe (riftwii/sdwrite.hpp) with injectable operations.
#include <cerrno>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/sdwrite.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using riftwii::SdWriteKind;
using riftwii::classify_sd_write_errno;

static void TestClassify() {
    EXPECT_TRUE(classify_sd_write_errno(0) == SdWriteKind::Fine);
    EXPECT_TRUE(classify_sd_write_errno(ENOSPC) == SdWriteKind::Full);
    EXPECT_TRUE(classify_sd_write_errno(EROFS) == SdWriteKind::Locked);
    EXPECT_TRUE(classify_sd_write_errno(EACCES) == SdWriteKind::Other);
    EXPECT_TRUE(classify_sd_write_errno(EPERM) == SdWriteKind::Other);
    EXPECT_TRUE(classify_sd_write_errno(EIO) == SdWriteKind::Other);
    EXPECT_TRUE(classify_sd_write_errno(EINVAL) == SdWriteKind::Other);
    EXPECT_TRUE(classify_sd_write_errno(EEXIST) == SdWriteKind::Other);
    EXPECT_TRUE(classify_sd_write_errno(ENOENT) == SdWriteKind::Other);
    EXPECT_TRUE(classify_sd_write_errno(-12345) == SdWriteKind::Other);
}

struct Fake {
    std::vector<std::string> calls;
    std::vector<int> open_ret;
    std::vector<int> mkdir_ret;
    std::vector<int> write_ret;
    std::vector<int> close_ret;
    std::vector<int> remove_ret;
    std::vector<int> free_ret;
    std::vector<unsigned long long> free_vals;
    size_t open_i = 0;
    size_t mkdir_i = 0;
    size_t write_i = 0;
    size_t close_i = 0;
    size_t remove_i = 0;
    size_t free_i = 0;
    int next_fd = 0;
};

static int NextRet(std::vector<int>& seq, size_t& pos) {
    if (pos < seq.size()) {
        return seq[pos++];
    }
    return 0;
}

static int FakeOpen(void* ctx, int* fd_out) {
    Fake* f = static_cast<Fake*>(ctx);
    f->calls.push_back("open");
    int r = NextRet(f->open_ret, f->open_i);
    if (r == 0) {
        f->next_fd++;
        *fd_out = f->next_fd;
    }
    return r;
}

static int FakeMkdir(void* ctx) {
    Fake* f = static_cast<Fake*>(ctx);
    f->calls.push_back("mkdir");
    return NextRet(f->mkdir_ret, f->mkdir_i);
}

static int FakeWrite(void* ctx, int) {
    Fake* f = static_cast<Fake*>(ctx);
    f->calls.push_back("write");
    return NextRet(f->write_ret, f->write_i);
}

static int FakeClose(void* ctx, int) {
    Fake* f = static_cast<Fake*>(ctx);
    f->calls.push_back("close");
    return NextRet(f->close_ret, f->close_i);
}

static int FakeRemove(void* ctx) {
    Fake* f = static_cast<Fake*>(ctx);
    f->calls.push_back("remove");
    return NextRet(f->remove_ret, f->remove_i);
}

static int FakeFree(void* ctx, unsigned long long* bytes_out) {
    Fake* f = static_cast<Fake*>(ctx);
    f->calls.push_back("free");
    int r = EIO;
    if (f->free_i < f->free_ret.size()) {
        r = f->free_ret[f->free_i];
    }
    unsigned long long v = 1024ULL * 1024ULL;
    if (f->free_i < f->free_vals.size()) {
        v = f->free_vals[f->free_i];
    }
    f->free_i++;
    if (r == 0 && bytes_out != nullptr) {
        *bytes_out = v;
    }
    return r;
}

static riftwii::SdWriteOps MakeOps(Fake* f) {
    riftwii::SdWriteOps ops{};
    ops.ctx = f;
    ops.open_file = FakeOpen;
    ops.make_dir = FakeMkdir;
    ops.write_file = FakeWrite;
    ops.close_file = FakeClose;
    ops.remove_file = FakeRemove;
    ops.free_bytes = FakeFree;
    return ops;
}

static bool CallsAre(const Fake& f, const std::vector<std::string>& want) {
    return f.calls == want;
}

static void TestFine() {
    riftwii::SdWriteProbeReset();
    Fake f;
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.did_probe);
    EXPECT_TRUE(out.kind == SdWriteKind::Fine);
    EXPECT_TRUE(out.error == 0);
    EXPECT_TRUE(CallsAre(f, {"open", "write", "close", "remove"}));
}

static void TestFullAtWrite() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {ENOSPC, ENOSPC};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Full);
    EXPECT_TRUE(out.error == ENOSPC);
    EXPECT_TRUE(!out.free_bytes_known);
    EXPECT_TRUE(CallsAre(f, {"open", "write", "close", "remove", "open", "write", "close", "remove", "free"}));
}

static void TestFullAtOpen() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.open_ret = {ENOSPC, ENOSPC};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Full);
    EXPECT_TRUE(out.error == ENOSPC);
    EXPECT_TRUE(!out.free_bytes_known);
    EXPECT_TRUE(CallsAre(f, {"open", "open", "free"}));
}

static void TestEnoentMkdirOk() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.open_ret = {ENOENT, 0};
    f.mkdir_ret = {0};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Fine);
    EXPECT_TRUE(CallsAre(f, {"open", "mkdir", "open", "write", "close", "remove"}));
}

static void TestEnoentMkdirEnospc() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.open_ret = {ENOENT, ENOENT};
    f.mkdir_ret = {ENOSPC, ENOSPC};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Full);
    EXPECT_TRUE(out.error == ENOSPC);
    EXPECT_TRUE(!out.free_bytes_known);
    EXPECT_TRUE(CallsAre(f, {"open", "mkdir", "open", "mkdir", "free"}));
}

static void TestErofsLocked() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {EROFS, EROFS};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Locked);
    EXPECT_TRUE(out.error == EROFS);
}

static void TestEaccesOther() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {EACCES, EACCES};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Other);
    EXPECT_TRUE(out.error == EACCES);
}

static void TestEpermOther() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {EPERM, EPERM};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Other);
    EXPECT_TRUE(out.error == EPERM);
}

static void TestRemoveFailureFine() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.remove_ret = {EIO};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Fine);
    EXPECT_TRUE(out.error == 0);
    EXPECT_TRUE(out.remove_error == EIO);
    EXPECT_TRUE(CallsAre(f, {"open", "write", "close", "remove"}));
}

static void TestRetrySuccess() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {ENOSPC, 0};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Fine);
    EXPECT_TRUE(out.error == 0);
    EXPECT_TRUE(CallsAre(f, {"open", "write", "close", "remove", "open", "write", "close", "remove"}));
}

static void TestRetryFirstRemoveObserved() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {ENOSPC, 0};
    f.remove_ret = {EIO, 0};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Fine);
    EXPECT_TRUE(out.first_remove_error == EIO);
    EXPECT_TRUE(out.remove_error == 0);
}

static void TestSecondErrorWins() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {ENOSPC, EROFS};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Locked);
    EXPECT_TRUE(out.error == EROFS);
}

static void TestGuardAndReset() {
    riftwii::SdWriteProbeReset();
    Fake f;
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    size_t after_first = f.calls.size();
    EXPECT_TRUE(after_first == 4);
    riftwii::SdWriteResult second{};
    EXPECT_TRUE(!riftwii::SdWriteProbe(true, MakeOps(&f), &second));
    EXPECT_TRUE(!second.did_probe);
    EXPECT_TRUE(f.calls.size() == after_first);
    riftwii::SdWriteProbeReset();
    riftwii::SdWriteResult third{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &third));
    EXPECT_TRUE(third.did_probe);
    EXPECT_TRUE(third.kind == SdWriteKind::Fine);
    EXPECT_TRUE(f.calls.size() == after_first + 4);
}

static void TestWriteBeatsClose() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {EIO, EIO};
    f.close_ret = {ENOSPC, ENOSPC};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.error == EIO);
    EXPECT_TRUE(out.kind == SdWriteKind::Other);
}

static void TestCloseFailure() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.close_ret = {ENOSPC, ENOSPC};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Full);
    EXPECT_TRUE(out.error == ENOSPC);
    EXPECT_TRUE(!out.free_bytes_known);
    EXPECT_TRUE(CallsAre(f, {"open", "write", "close", "remove", "open", "write", "close", "remove", "free"}));
}

static void TestMkdirEexist() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.open_ret = {ENOENT, 0};
    f.mkdir_ret = {EEXIST};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Fine);
    EXPECT_TRUE(CallsAre(f, {"open", "mkdir", "open", "write", "close", "remove"}));
}

static void TestReopenFailure() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.open_ret = {ENOENT, ENOSPC, ENOENT, ENOSPC};
    f.mkdir_ret = {0, 0};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Full);
    EXPECT_TRUE(out.error == ENOSPC);
    EXPECT_TRUE(!out.free_bytes_known);
    EXPECT_TRUE(CallsAre(f, {"open", "mkdir", "open", "open", "mkdir", "open", "free"}));
}

static void TestUnmounted() {
    riftwii::SdWriteProbeReset();
    Fake f;
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(false, MakeOps(&f), &out));
    EXPECT_TRUE(out.did_probe);
    EXPECT_TRUE(out.kind == SdWriteKind::Fine);
    EXPECT_TRUE(f.calls.empty());
    riftwii::SdWriteResult skipped{};
    EXPECT_TRUE(!riftwii::SdWriteProbe(true, MakeOps(&f), &skipped));
    EXPECT_TRUE(f.calls.empty());
}

static void TestEioFullWhenEmpty() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {EIO, EIO};
    f.free_ret = {0};
    f.free_vals = {0};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Full);
    EXPECT_TRUE(out.error == EIO);
    EXPECT_TRUE(out.free_bytes_known);
    EXPECT_TRUE(out.free_bytes == 0);
    EXPECT_TRUE(CallsAre(f, {"open", "write", "close", "remove", "open", "write", "close", "remove", "free"}));
}

static void TestEioOtherWhenSpace() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {EIO, EIO};
    f.free_ret = {0};
    f.free_vals = {1024ULL * 1024ULL};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Other);
    EXPECT_TRUE(out.error == EIO);
    EXPECT_TRUE(out.free_bytes_known);
    EXPECT_TRUE(out.free_bytes == 1024ULL * 1024ULL);
    EXPECT_TRUE(CallsAre(f, {"open", "write", "close", "remove", "open", "write", "close", "remove", "free"}));
}

static void TestEioOtherWhenFreeUnknown() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {EIO, EIO};
    f.free_ret = {EIO};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Other);
    EXPECT_TRUE(out.error == EIO);
    EXPECT_TRUE(!out.free_bytes_known);
    EXPECT_TRUE(out.free_bytes == 0);
}

static void TestEnospcFullWhenFreeUnknown() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {ENOSPC, ENOSPC};
    f.free_ret = {EIO};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Full);
    EXPECT_TRUE(out.error == ENOSPC);
    EXPECT_TRUE(!out.free_bytes_known);
    EXPECT_TRUE(out.free_bytes == 0);
}

static void TestErofsSkipsFreeQuery() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {EROFS, EROFS};
    f.free_ret = {0};
    f.free_vals = {0};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Locked);
    EXPECT_TRUE(out.error == EROFS);
    EXPECT_TRUE(!out.free_bytes_known);
    EXPECT_TRUE(f.free_i == 0);
    EXPECT_TRUE(CallsAre(f, {"open", "write", "close", "remove", "open", "write", "close", "remove"}));
}

static void TestSuccessSkipsFreeQuery() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.free_ret = {0};
    f.free_vals = {0};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Fine);
    EXPECT_TRUE(!out.free_bytes_known);
    EXPECT_TRUE(f.free_i == 0);
    EXPECT_TRUE(CallsAre(f, {"open", "write", "close", "remove"}));
}

static void TestRetrySuccessSkipsFreeQuery() {
    riftwii::SdWriteProbeReset();
    Fake f;
    f.write_ret = {ENOSPC, 0};
    f.free_ret = {0};
    f.free_vals = {0};
    riftwii::SdWriteResult out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&f), &out));
    EXPECT_TRUE(out.kind == SdWriteKind::Fine);
    EXPECT_TRUE(out.error == 0);
    EXPECT_TRUE(!out.free_bytes_known);
    EXPECT_TRUE(f.free_i == 0);
}

static void TestFreeBoundary() {
    riftwii::SdWriteProbeReset();
    Fake under;
    under.write_ret = {EIO, EIO};
    under.free_ret = {0};
    under.free_vals = {64ULL * 1024ULL - 1ULL};
    riftwii::SdWriteResult under_out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&under), &under_out));
    EXPECT_TRUE(under_out.kind == SdWriteKind::Full);
    EXPECT_TRUE(under_out.free_bytes_known);
    EXPECT_TRUE(under_out.free_bytes == 64ULL * 1024ULL - 1ULL);
    riftwii::SdWriteProbeReset();
    Fake exact;
    exact.write_ret = {EIO, EIO};
    exact.free_ret = {0};
    exact.free_vals = {64ULL * 1024ULL};
    riftwii::SdWriteResult exact_out{};
    EXPECT_TRUE(riftwii::SdWriteProbe(true, MakeOps(&exact), &exact_out));
    EXPECT_TRUE(exact_out.kind == SdWriteKind::Other);
    EXPECT_TRUE(exact_out.free_bytes_known);
    EXPECT_TRUE(exact_out.free_bytes == 64ULL * 1024ULL);
}

int main() {
    TestClassify();
    TestFine();
    TestFullAtWrite();
    TestFullAtOpen();
    TestEnoentMkdirOk();
    TestEnoentMkdirEnospc();
    TestErofsLocked();
    TestEaccesOther();
    TestEpermOther();
    TestRemoveFailureFine();
    TestRetrySuccess();
    TestRetryFirstRemoveObserved();
    TestSecondErrorWins();
    TestGuardAndReset();
    TestWriteBeatsClose();
    TestCloseFailure();
    TestMkdirEexist();
    TestReopenFailure();
    TestUnmounted();
    TestEioFullWhenEmpty();
    TestEioOtherWhenSpace();
    TestEioOtherWhenFreeUnknown();
    TestEnospcFullWhenFreeUnknown();
    TestErofsSkipsFreeQuery();
    TestSuccessSkipsFreeQuery();
    TestRetrySuccessSkipsFreeQuery();
    TestFreeBoundary();
    if (g_failures == 0) std::cout << "sdwrite: all tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
