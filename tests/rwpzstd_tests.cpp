// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The real plugin shim, with the loader's decoder callbacks mocked. Cover
// error transport and RVZ wording without replacing the host's libc.
#include "rwpruntime.hpp"
#include "rvz_error.hpp"
#include "zstd.h"

#include <cstdint>
#include <iostream>
#include <string>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

rwp_host g_host{};
std::size_t g_decode_result = 0;
const char* g_error_name = "Error strings stripped";
unsigned g_decode_calls = 0;

std::size_t Decode(void* to, std::size_t to_bytes, const void* from, std::size_t from_bytes) {
    EXPECT_TRUE(to != nullptr);
    EXPECT_TRUE(from != nullptr);
    EXPECT_EQ(to_bytes, std::size_t(8));
    EXPECT_EQ(from_bytes, std::size_t(4));
    ++g_decode_calls;
    return g_decode_result;
}

unsigned long long FrameSize(const void* from, std::size_t from_bytes) {
    EXPECT_TRUE(from != nullptr);
    EXPECT_EQ(from_bytes, std::size_t(4));
    return 8;
}

std::uint32_t IsError(std::size_t result) {
    // Match the loader's contract, including values near size_t's maximum.
    return result > std::size_t(0) - static_cast<std::size_t>(ZSTD_error_maxCode);
}

const char* ErrorName(std::size_t result) {
    EXPECT_EQ(result, g_decode_result);
    return g_error_name;
}

struct ErrorCase {
    ZSTD_ErrorCode code;
    const char* name;
};

void TestErrors() {
    const ErrorCase cases[] = {
        {ZSTD_error_GENERIC, "unspecified failure"},
        {ZSTD_error_prefix_unknown, "unrecognized frame"},
        {ZSTD_error_version_unsupported, "unsupported version"},
        {ZSTD_error_frameParameter_unsupported, "unsupported frame parameter"},
        {ZSTD_error_frameParameter_windowTooLarge, "window too large"},
        {ZSTD_error_corruption_detected, "corruption detected"},
        {ZSTD_error_checksum_wrong, "checksum mismatch"},
        {ZSTD_error_literals_headerWrong, "invalid literals header"},
        {ZSTD_error_dictionary_corrupted, "corrupt dictionary"},
        {ZSTD_error_dictionary_wrong, "wrong dictionary"},
        {ZSTD_error_dictionaryCreation_failed, "dictionary creation failed"},
        {ZSTD_error_parameter_unsupported, "unsupported parameter"},
        {ZSTD_error_parameter_combination_unsupported, "unsupported parameter combination"},
        {ZSTD_error_parameter_outOfBound, "parameter out of bounds"},
        {ZSTD_error_tableLog_tooLarge, "table log too large"},
        {ZSTD_error_maxSymbolValue_tooLarge, "maximum symbol value too large"},
        {ZSTD_error_maxSymbolValue_tooSmall, "maximum symbol value too small"},
        {ZSTD_error_cannotProduce_uncompressedBlock, "cannot produce an uncompressed block"},
        {ZSTD_error_stabilityCondition_notRespected, "buffer stability condition not respected"},
        {ZSTD_error_stage_wrong, "wrong decoder stage"},
        {ZSTD_error_init_missing, "decoder not initialized"},
        {ZSTD_error_memory_allocation, "allocation failed"},
        {ZSTD_error_workSpace_tooSmall, "workspace too small"},
        {ZSTD_error_dstSize_tooSmall, "output capacity too small"},
        {ZSTD_error_srcSize_wrong, "wrong input size"},
        {ZSTD_error_dstBuffer_null, "null output buffer"},
        {ZSTD_error_noForwardProgress_destFull, "no progress because the output buffer is full"},
        {ZSTD_error_noForwardProgress_inputEmpty, "no progress because the input is empty"},
        {ZSTD_error_frameIndex_tooLarge, "frame index too large"},
        {ZSTD_error_seekableIO, "read or seek failed"},
        {ZSTD_error_dstBuffer_wrong, "invalid output buffer"},
        {ZSTD_error_srcBuffer_wrong, "invalid input buffer"},
        {ZSTD_error_sequenceProducer_failed, "sequence producer failed"},
        {ZSTD_error_externalSequences_invalid, "invalid external sequences"}
    };
    std::uint8_t input[4]{};
    std::uint8_t output[8]{};
    ZSTD_DCtx* context = ZSTD_createDCtx();
    EXPECT_TRUE(context != nullptr);
    EXPECT_EQ(ZSTD_getFrameContentSize(input, sizeof(input)), 8ull);
    for (const ErrorCase& item : cases) {
        g_decode_result = std::size_t(0) - static_cast<std::size_t>(item.code);
        const std::size_t result = ZSTD_decompressDCtx(context, output, sizeof(output), input, sizeof(input));
        EXPECT_EQ(result, g_decode_result);
        EXPECT_TRUE(ZSTD_isError(result));
        EXPECT_EQ(ZSTD_getErrorCode(result), item.code);
        const std::string expected = "Zstandard error code " + std::to_string(static_cast<unsigned>(item.code)) +
                                     " (" + item.name + ")";
        EXPECT_EQ(riftwii::rvz_zstd_error(ZSTD_getErrorCode(result), ZSTD_getErrorName(result)), expected);
    }
    EXPECT_EQ(g_decode_calls, sizeof(cases) / sizeof(cases[0]));
    EXPECT_EQ(ZSTD_freeDCtx(context), std::size_t(0));
}

void TestFallbackAndNativeText() {
    g_decode_result = std::size_t(0) - 99;
    const ZSTD_ErrorCode code = ZSTD_getErrorCode(g_decode_result);
    EXPECT_EQ(static_cast<unsigned>(code), 99u);
    EXPECT_EQ(riftwii::rvz_zstd_error(code, ZSTD_getErrorName(g_decode_result)),
              std::string("Zstandard error code 99"));

    // A loader with error strings must keep its wording, even for a code
    // newer than the plugin's fallback table. The same formatter serves hosts.
    g_error_name = "Decoder-specific failure detail";
    EXPECT_EQ(riftwii::rvz_zstd_error(code, ZSTD_getErrorName(g_decode_result)),
              std::string("Zstandard error code 99 (Decoder-specific failure detail)"));
    g_decode_result = std::size_t(0) - static_cast<std::size_t>(ZSTD_error_corruption_detected);
    EXPECT_EQ(riftwii::rvz_zstd_error(ZSTD_getErrorCode(g_decode_result), ZSTD_getErrorName(g_decode_result)),
              std::string("Zstandard error code 20 (Decoder-specific failure detail)"));
}

void TestSuccess() {
    std::uint8_t input[4]{};
    std::uint8_t output[8]{};
    g_decode_result = 8;
    const std::size_t result = ZSTD_decompress(output, sizeof(output), input, sizeof(input));
    EXPECT_EQ(result, std::size_t(8));
    EXPECT_TRUE(!ZSTD_isError(result));
    EXPECT_EQ(ZSTD_getErrorCode(result), ZSTD_error_no_error);
    EXPECT_EQ(ZSTD_getErrorCode(0), ZSTD_error_no_error);
}

}  // namespace

namespace riftwii::plugin {

const rwp_host& host() { return g_host; }

}  // namespace riftwii::plugin

int main() {
    g_host.zstd_decompress = Decode;
    g_host.zstd_frame_size = FrameSize;
    g_host.zstd_is_error = IsError;
    g_host.zstd_error_name = ErrorName;
    TestErrors();
    TestFallbackAndNativeText();
    TestSuccess();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "rwpzstd: all tests passed" << std::endl;
    return 0;
}
