// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <setjmp.h>
#include <string.h>
#include "smallblob_model.h"
#define RTSTUB_HOST_TEST
#define RTSTUB_IPC_POLLS 3u
#ifdef STUB_LEGACY
#define rtstub_main legacy_rtstub_main
#define rtstub_count legacy_rtstub_count
#define rtstub_result legacy_rtstub_result
#define rtstub_titles legacy_rtstub_titles
#define stub_model_run legacy_stub_model_run
#include "fixtures/small-blobs/legacy_stub_main.c"
#else
#include "../runtime/stub/stub_main.c"
#endif
uint32_t rtstub_count;
int32_t rtstub_result;
uint8_t rtstub_titles[RTSTUB_TITLE_BYTES * RTSTUB_MAX_TITLES] __attribute__((aligned(32)));

static const struct stub_model_script* model_script;
static struct stub_model_trace* model_trace;
static const volatile void* model_pointers[32];
static uint32_t model_pointer_count, model_control, model_message, model_stale;
static uint32_t model_forever;
static jmp_buf model_exit;

static uint32_t rtstub_physical(const volatile void* pointer) {
    uint32_t i;
    for (i = 0; i < model_pointer_count; ++i) {
        if (model_pointers[i] == pointer) return (i + 1u) * 32u;
    }
    model_pointers[model_pointer_count++] = pointer;
    return model_pointer_count * 32u;
}
static const void* model_pointer(uint32_t address) {
    return (const void*)model_pointers[address / 32u - 1u];
}
static uint32_t rtstub_read32(uint32_t address) {
    ++model_trace->polls;
    if (model_trace->polls > 1000u) longjmp(model_exit, 1);
    if (address == RTSTUB_IPC_ARMMSG) return model_stale ? 0xdead0000u : model_message;
    if (model_stale) return RTSTUB_IPC_Y1;
    if (model_forever) {
        model_trace->accepted = model_forever;
        longjmp(model_exit, 1);  /* IOS has taken over the CPU. */
    }
    return model_control;
}
static void rtstub_write32(uint32_t address, uint32_t value) {
    if (address == RTSTUB_IPC_PPCMSG) model_message = value;
    if (address != RTSTUB_IPC_PPCCTRL) return;
    if (value == RTSTUB_IPC_X2 && model_stale) {
        --model_stale;
        return;
    }
    if (value == RTSTUB_IPC_Y1 || value == RTSTUB_IPC_X2) model_control &= ~RTSTUB_IPC_Y1;
    if (value == RTSTUB_IPC_Y2) {
        model_control &= ~RTSTUB_IPC_Y2;
        model_control |= RTSTUB_IPC_Y1;
        return;
    }
    if (value == RTSTUB_IPC_X1) {
        struct rtstub_request* request = (struct rtstub_request*)model_pointer(model_message);
        struct shot_model_event* event = &model_trace->events[model_trace->count++];
        event->kind = request->command;
        event->fd = request->fd;
        if (request->command == RTSTUB_IOS_OPEN) {
            const void* path = model_pointer(request->args[0]);
            event->output_length = (uint32_t)((uintptr_t)path & 31u);
            strcpy((char*)event->data, path);
        } else if (request->command == RTSTUB_IOS_IOCTLV) {
            const uint32_t* vectors = model_pointer(request->args[3]);
            event->command = request->args[0];
            event->length = vectors[1];
            event->output_length = vectors[3];
            memcpy(event->data, model_pointer(vectors[0]), 8u);
            memcpy(event->data + 8, model_pointer(vectors[2]), RTSTUB_VIEW_BYTES);
            if (model_script->accept == model_trace->count) model_forever = model_trace->count;
        } else {
            event->command = request->args[0];
            event->length = request->args[2];
            event->output_length = request->args[4];
            memcpy(event->data, model_pointer(request->args[1]), 32u);
            memcpy(event->data + 32, model_pointer(request->args[3]), 32u);
        }
        request->result = model_script->answers[model_trace->count - 1u];
        model_control = model_trace->count == model_script->timeout_at ? 0 : RTSTUB_IPC_Y2;
    }
}
void stub_model_run(const struct stub_model_script* script, struct stub_model_trace* trace) {
    uint32_t i;
    model_script = script;
    model_trace = trace;
    memset(trace, 0, sizeof(*trace));
    model_pointer_count = 0;
    model_control = 0;
    model_message = 0;
    model_stale = script->stale;
    model_forever = 0;
    rtstub_count = script->count;
    rtstub_result = 0;
    for (i = 0; i < sizeof(rtstub_titles); ++i) rtstub_titles[i] = (uint8_t)(i * 7u);
    if (setjmp(model_exit) == 0) rtstub_main();
    trace->result = rtstub_result;
}
