// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RIFTWII_SMALLBLOB_MODEL_H
#define RIFTWII_SMALLBLOB_MODEL_H
#include <stdint.h>

struct shot_model_script {
    uint32_t failure_at;  /* One-based IOS submission; zero disables. */
    int32_t failure;
    uint32_t immediate;
    uint32_t probe_taken;
    uint32_t part_taken;
};
struct shot_model_event {
    uint32_t kind, command, length, output_length;
    int32_t fd;
    uint8_t data[256];
};
struct shot_model_trace {
    struct shot_model_event events[512];
    uint32_t count, phase, shots, failures, number;
    int32_t error;
};
struct fault_model_trace {
    struct shot_model_event events[8];
    uint32_t count, step, answers;
    int32_t result;
    uint8_t bca[97][96];
    uint32_t output, output_length;
};
struct stub_model_script {
    uint32_t count, accept, timeout_at, stale;
    int32_t answers[6];
};
struct stub_model_trace {
    struct shot_model_event events[8];
    uint32_t count, accepted, polls;
    int32_t result;
};
#ifdef __cplusplus
extern "C" {
#endif
void fault_model_run(void* memory, uint32_t failure_at, int32_t failure, struct fault_model_trace* trace);
void legacy_fault_model_run(void* memory, uint32_t failure_at, int32_t failure, struct fault_model_trace* trace);
void stub_model_run(const struct stub_model_script* script, struct stub_model_trace* trace);
void legacy_stub_model_run(const struct stub_model_script* script, struct stub_model_trace* trace);
void shot_model_run(void* memory, const struct shot_model_script* script, struct shot_model_trace* trace);
void legacy_shot_model_run(void* memory, const struct shot_model_script* script, struct shot_model_trace* trace);
#ifdef __cplusplus
}
#endif
#endif
