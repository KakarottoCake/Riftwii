# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(engine reference current)
    execute_process(COMMAND "${${engine}}" "${trace}-${engine}.bin"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${engine} failed (${result}): ${output}${errors}")
    endif()
    message(STATUS "${engine}: ${output}")
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
    "${trace}-reference.bin" "${trace}-current.bin" RESULT_VARIABLE mismatch)
if(NOT mismatch EQUAL 0)
    message(FATAL_ERROR "File-service transcripts differ: ${trace}-*.bin")
endif()
file(REMOVE "${trace}-reference.bin" "${trace}-current.bin")
