// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "zstd_errors.h"

namespace riftwii {

// Keep every error in the vendored zstd_errors.h readable when the decoder
// omits its strings. no_error and maxCode are not failures to describe.
inline const char* rvz_zstd_error_name(ZSTD_ErrorCode code) {
    switch (code) {
    case ZSTD_error_GENERIC: return "unspecified failure";
    case ZSTD_error_prefix_unknown: return "unrecognized frame";
    case ZSTD_error_version_unsupported: return "unsupported version";
    case ZSTD_error_frameParameter_unsupported: return "unsupported frame parameter";
    case ZSTD_error_frameParameter_windowTooLarge: return "window too large";
    case ZSTD_error_corruption_detected: return "corruption detected";
    case ZSTD_error_checksum_wrong: return "checksum mismatch";
    case ZSTD_error_literals_headerWrong: return "invalid literals header";
    case ZSTD_error_dictionary_corrupted: return "corrupt dictionary";
    case ZSTD_error_dictionary_wrong: return "wrong dictionary";
    case ZSTD_error_dictionaryCreation_failed: return "dictionary creation failed";
    case ZSTD_error_parameter_unsupported: return "unsupported parameter";
    case ZSTD_error_parameter_combination_unsupported: return "unsupported parameter combination";
    case ZSTD_error_parameter_outOfBound: return "parameter out of bounds";
    case ZSTD_error_tableLog_tooLarge: return "table log too large";
    case ZSTD_error_maxSymbolValue_tooLarge: return "maximum symbol value too large";
    case ZSTD_error_maxSymbolValue_tooSmall: return "maximum symbol value too small";
    case ZSTD_error_cannotProduce_uncompressedBlock: return "cannot produce an uncompressed block";
    case ZSTD_error_stabilityCondition_notRespected: return "buffer stability condition not respected";
    case ZSTD_error_stage_wrong: return "wrong decoder stage";
    case ZSTD_error_init_missing: return "decoder not initialized";
    case ZSTD_error_memory_allocation: return "allocation failed";
    case ZSTD_error_workSpace_tooSmall: return "workspace too small";
    case ZSTD_error_dstSize_tooSmall: return "output capacity too small";
    case ZSTD_error_srcSize_wrong: return "wrong input size";
    case ZSTD_error_dstBuffer_null: return "null output buffer";
    case ZSTD_error_noForwardProgress_destFull: return "no progress because the output buffer is full";
    case ZSTD_error_noForwardProgress_inputEmpty: return "no progress because the input is empty";
    case ZSTD_error_frameIndex_tooLarge: return "frame index too large";
    case ZSTD_error_seekableIO: return "read or seek failed";
    case ZSTD_error_dstBuffer_wrong: return "invalid output buffer";
    case ZSTD_error_srcBuffer_wrong: return "invalid input buffer";
    case ZSTD_error_sequenceProducer_failed: return "sequence producer failed";
    case ZSTD_error_externalSequences_invalid: return "invalid external sequences";
    default: return nullptr;
    }
}

inline std::string rvz_zstd_error(ZSTD_ErrorCode code, const char* decoder_name) {
    // A decoder with strings may know more than this build's fallback table.
    // Preserve its wording on hosts and on plugins using such a loader.
    const char* name = decoder_name;
    if (name == nullptr || name[0] == '\0' || std::string_view(name) == "Error strings stripped") {
        name = rvz_zstd_error_name(code);
    }
    std::string message = "Zstandard error code " + std::to_string(static_cast<unsigned>(code));
    if (name != nullptr) {
        message += " (";
        message += name;
        message += ")";
    }
    return message;
}

}  // namespace riftwii
