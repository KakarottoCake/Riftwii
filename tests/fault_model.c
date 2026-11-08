// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "smallblob_model.h"
#define RTFAULT_HOST_TEST
#ifdef FAULT_LEGACY
#include "fixtures/small-blobs/legacy_fault_hook.c"
#define fault_model_run legacy_fault_model_run
#else
#include "../runtime/fault/fault_hook.c"
#endif
static struct fault_model_trace* model_trace;
static uint32_t model_failure_at;
static int32_t model_failure;
static int32_t call(struct rt_fault_context* context, uint32_t* request) {
    struct shot_model_event* event = &model_trace->events[model_trace->count++];
    const uint8_t* data = (const uint8_t*)(uintptr_t)(request[3] | 0x80000000u);
    (void)context;
    event->kind = request[0];
    event->fd = request[2];
    event->length = request[4];
    if (request[0] == IOS_OPEN) strcpy((char*)event->data, (const char*)data);
    if (request[0] == IOS_IOCTL) {
        data = (const uint8_t*)(uintptr_t)(request[4] | 0x80000000u);
        event->command = request[3];
        event->length = request[5];
        memcpy(event->data, data, request[5]);
    }
    if (request[0] == IOS_WRITE) {
        /* Retain both halves of the 512-byte record in separate events. */
        memcpy(event->data, data, 256u);
        memcpy(model_trace->events[7].data, data + 256, 256u);
    }
    if (model_trace->count == model_failure_at) return model_failure;
    if (request[0] == IOS_OPEN) return request[4] ? 11 : 10;
    if (request[0] == IOS_WRITE) return request[4];
    return 0;
}
void fault_model_run(void* memory, uint32_t failure_at, int32_t failure, struct fault_model_trace* trace) {
    struct rt_fault_context context;
    struct rt_fault_state* state = memory;
    uint32_t args[8];
    uint32_t* input = (uint32_t*)((uint8_t*)memory + 0x11000);
    uint8_t* output = (uint8_t*)memory + 0x10000;
    uint32_t length, i;
    memset(&context, 0, sizeof(context));
    memset(state, 0xa5, sizeof(*state));
    memset(trace, 0, sizeof(*trace));
    context.state = (uint32_t)(uintptr_t)state;
    for (i = 0; i < sizeof(state->record); ++i) ((uint8_t*)&state->record)[i] = (uint8_t)(i * 13u);
    model_trace = trace;
    model_failure_at = failure_at;
    model_failure = failure;
    save(&context, state);
    trace->step = context.step;
    trace->result = context.result;

    /* Include undersized, exact and oversized BCA replies. Guard bytes
     * after the bounded answer must remain untouched. */
    context.flags = RT_FAULT_FLAG_BCA;
    context.bca_sink = 0x91234560u;
    input[0] = RT_FAULT_DI_READ_BCA << 24;
    for (length = 0; length <= 96u; ++length) {
        memset(output, 0xa5, 96u);
        memset(args, 0, sizeof(args));
        args[1] = RT_FAULT_DI_READ_BCA;
        args[2] = (uint32_t)(uintptr_t)input;
        args[3] = 32;
        args[4] = (uint32_t)(uintptr_t)output;
        args[5] = length;
#ifdef FAULT_LEGACY
        legacy_fault_on_ioctl(&context, args);
#else
        fault_on_ioctl(&context, args);
#endif
        memcpy(trace->bca[length], output, 96u);
    }
    trace->answers = context.bca_answers;
    trace->output = args[4];
    trace->output_length = args[5];
}
