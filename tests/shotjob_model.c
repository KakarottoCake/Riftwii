// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "smallblob_model.h"
#define RTSHOT_HOST_TEST
#ifdef SHOT_LEGACY
#include "fixtures/small-blobs/legacy_shot_hook.c"
#define shot_model_run legacy_shot_model_run
#else
#include "../runtime/shot/shot_hook.c"
#endif

static const struct shot_model_script* model_script;
static struct shot_model_trace* model_trace;
static int32_t model_answer;
static uint32_t model_probes, model_parts;

static int32_t model_send(uint32_t kind, int32_t fd, uint32_t command, const void* data,
                          uint32_t length, uint32_t output_length, int32_t answer) {
    struct shot_model_event* event = &model_trace->events[model_trace->count++];
    event->kind = kind;
    event->fd = fd;
    event->command = command;
    event->length = length;
    event->output_length = output_length;
    if (kind == 1u || command == FS_GET_ATTR || command == FS_DELETE) {
        strcpy((char*)event->data, data);
    } else if (command == FS_RENAME) {
        strcpy((char*)event->data, data);
        strcpy((char*)event->data + 64, (const char*)data + 64);
    } else if (data != 0) {
        memcpy(event->data, data, length);
    }
    if (model_trace->count == model_script->failure_at) {
        answer = model_script->failure;
        if (model_script->immediate) return answer;
    }
    model_answer = answer;
    return 0;
}
static int32_t model_open(const char* path, uint32_t mode, uint32_t callback, uint32_t user) {
    (void)callback;
    (void)user;
    return model_send(1, 0, mode, path, 0, 0, mode ? 11 : 10);
}
static int32_t model_close(int32_t fd, uint32_t callback, uint32_t user) {
    (void)callback;
    (void)user;
    return model_send(2, fd, 0, 0, 0, 0, 0);
}
static int32_t model_write(int32_t fd, const void* data, uint32_t length, uint32_t callback, uint32_t user) {
    (void)callback;
    (void)user;
    return model_send(4, fd, 0, data, length, 0, (int32_t)length);
}
static int32_t model_ioctl(int32_t fd, uint32_t command, void* data, uint32_t length,
                           void* output, uint32_t output_length, uint32_t callback, uint32_t user) {
    int32_t answer = 0;
    (void)output;
    (void)callback;
    (void)user;
    if (command == FS_GET_ATTR) answer = model_probes++ < model_script->probe_taken ? 0 : FS_NOT_FOUND;
    if (command == FS_CREATE_FILE && model_parts++ < model_script->part_taken) answer = FS_EXISTS;
    return model_send(6, fd, command, data, length, output_length, answer);
}
static uintptr_t shot_host_api(uint32_t address) {
    switch (address) {
    case 1: return (uintptr_t)model_open;
    case 2: return (uintptr_t)model_close;
    case 3: return (uintptr_t)model_write;
    default: return (uintptr_t)model_ioctl;
    }
}

void shot_model_run(void* memory, const struct shot_model_script* script, struct shot_model_trace* trace) {
    struct rt_shot_context context;
    struct rt_shot_state* state = memory;
    uint8_t* pixels = (uint8_t*)memory + 0x10000;
    uint32_t i;
    int32_t sent;
    memset(&context, 0, sizeof(context));
    memset(state, 0, sizeof(*state));
    memset(trace, 0, sizeof(*trace));
    context.state = (uint32_t)(uintptr_t)state;
    context.open_async = 1;
    context.close_async = 2;
    context.write_async = 3;
    context.ioctl_async = 4;
    context.flags = RT_SHOT_FLAG_DIRECT;
    context.inited = 1;
    context.number = 9998;
    state->fs_fd = -1;
    state->file_fd = -1;
    state->phase = RT_SHOT_OPEN_FS;
    state->bytes = RTSHOT_HEADER_BYTES;
    state->pixels = (uint32_t)(uintptr_t)pixels;
    state->pixel_bytes = 96;
    for (i = 0; i < RTSHOT_HEADER_BYTES; ++i) state->header[i] = (uint8_t)i;
    for (i = 0; i < state->pixel_bytes; ++i) pixels[i] = (uint8_t)(i * 7u);
    model_script = script;
    model_trace = trace;
    model_parts = 0;
    model_probes = 0;
    sent = model_open("/dev/fs", 0, 0, 0);
    job_step(&context, state, sent < 0 ? sent : model_answer);
    for (i = 0; state->phase != RT_SHOT_IDLE && i < 512u; ++i)
        job_step(&context, state, model_answer);
    trace->phase = state->phase;
    trace->shots = context.shots;
    trace->failures = context.failures;
    trace->number = context.number;
    trace->error = context.last_error;
}
