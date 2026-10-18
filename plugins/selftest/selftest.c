/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The selftest plugin (docs/PLUGINS.md, stage (a)): proves that code the
 * loader read from the card runs from MEM2, that its relocations were
 * applied, that its .bss was cleared and that each function of the host
 * table works. It has no feature of its own and is loaded only by the test
 * switch sd:/riftwii/plugin_test.txt (docs/DEVELOPING.md). */
#include "riftwii/plugin_api.h"

#define SELFTEST_VERSION 1u

/* A computation that goes through a table of function pointers in
 * .rodata: each pointer is an ADDR32 relocation, so a wrong or missing
 * relocation calls the wrong place and the value comes out wrong (or the
 * plugin crashes, which the crash report then names). */
typedef uint32_t (*step_fn)(uint32_t value, uint32_t operand);

static uint32_t step_add(uint32_t value, uint32_t operand) { return value + operand; }
static uint32_t step_xor(uint32_t value, uint32_t operand) { return value ^ operand; }
static uint32_t step_mul(uint32_t value, uint32_t operand) { return value * operand; }
static uint32_t step_rotate(uint32_t value, uint32_t operand) {
    return (value << operand) | (value >> (32u - operand));
}

typedef struct step {
    step_fn apply;
    uint32_t operand;
    const char* name;
} step;

static const step kSteps[] = {
    {step_add, 0x9E3779B9u, "add"}, {step_xor, 0xA5A5A5A5u, "xor"}, {step_mul, 0x01000193u, "multiply"},
    {step_rotate, 13u, "rotate"},   {step_add, 0x7F4A7C15u, "add"}, {step_mul, 0x2545F491u, "multiply"},
    {step_xor, 0x0F0F1234u, "xor"}, {step_rotate, 7u, "rotate"},
};
#define STEP_COUNT (sizeof(kSteps) / sizeof(kSteps[0]))
#define STEP_ROUNDS 3u
#define CHECK_START 0x12345678u
/* The same steps worked out on a PC (tools: python, three rounds). */
#define CHECK_EXPECTED 0x8AF4BB0Eu

#define TEST_FILE "sd:/riftwii/plugin_test.txt"
#define SCRATCH_BYTES 4096u

/* .bss: the loader clears it; a non-zero value here at init is a loader bug. */
static const rwp_host* g_host;
static uint32_t g_inits;

static uint32_t compute_check(void) {
    uint32_t value = CHECK_START;
    uint32_t round;
    uint32_t i;
    for (round = 0; round < STEP_ROUNDS; ++round) {
        for (i = 0; i < STEP_COUNT; ++i) value = kSteps[i].apply(value, kSteps[i].operand);
    }
    return value;
}

/* Whether the test file has a line that is exactly `word`. */
static int test_file_has(const char* text, int32_t length, const char* word) {
    int32_t start = 0;
    while (start < length) {
        int32_t end = start;
        int32_t i = 0;
        while (end < length && text[end] != '\n' && text[end] != '\r') ++end;
        while (word[i] != '\0' && start + i < end && text[start + i] == word[i]) ++i;
        if (word[i] == '\0' && start + i == end) return 1;
        start = end + 1;
    }
    return 0;
}

int32_t rwp_init(const rwp_host* host);
void rwp_shutdown(void);
static int32_t selftest_probe(const char* drive_root);

const rwp_plugin rwp_exports = {
    sizeof(rwp_plugin), RWP_API_VERSION, SELFTEST_VERSION, 0, selftest_probe, rwp_shutdown, 0,
};

int32_t rwp_init(const rwp_host* host) {
    uint32_t check;
    uint8_t* scratch;
    uint32_t sum = 0;
    uint32_t i;
    char text[256];
    int32_t length;

    /* A table shorter than this plugin's (an older loader) is refused
     * before any of its functions is called. */
    if (host->api_version != RWP_API_VERSION || !RWP_HOST_HAS(host, crash_register)) return 1;
    g_host = host;
    if (g_inits != 0) {
        host->log("its .bss was not cleared (init count %u); stopping", g_inits);
        return 2;
    }
    ++g_inits;
    host->log("version %u running from 0x%08x (its init), built for API %u with RiftWii %s; this RiftWii is %s",
              SELFTEST_VERSION, (uint32_t)(uintptr_t)&rwp_init, RWP_API_VERSION, host->build_stamp(), host->version());

    check = compute_check();
    host->log("check value 0x%08x through %u relocated steps (%s first), expected 0x%08x: %s", check,
              (uint32_t)(STEP_COUNT * STEP_ROUNDS), kSteps[0].name, CHECK_EXPECTED,
              check == CHECK_EXPECTED ? "code and relocations in MEM2 work" : "WRONG");
    if (check != CHECK_EXPECTED) return 3;

    scratch = (uint8_t*)host->arena_alloc(SCRATCH_BYTES);
    if (scratch == 0) {
        host->log("the arena had no %u bytes for it", SCRATCH_BYTES);
        return 4;
    }
    for (i = 0; i < SCRATCH_BYTES; ++i) scratch[i] = (uint8_t)(i * 7u);
    host->flush_range(scratch, SCRATCH_BYTES);
    for (i = 0; i < SCRATCH_BYTES; ++i) sum += scratch[i];
    host->log("arena block of %u bytes at 0x%08x, sum %u (expected %u)", SCRATCH_BYTES, (uint32_t)(uintptr_t)scratch,
              sum, 522240u);
    if (sum != 522240u) return 5;

    length = host->file_read(TEST_FILE, 0, text, sizeof(text));
    host->log("read %d bytes of %s (size %d)", length, TEST_FILE, host->file_size(TEST_FILE));
    if (host->crash_register("selftest scratch", (uint32_t)(uintptr_t)scratch, SCRATCH_BYTES) == 0) {
        host->log("the crash table took no range");
    }
    if (length > 0 && test_file_has(text, length, "crash")) {
        host->log("crashing on purpose (the test file asks for it): the crash report should name this plugin");
        __asm__ volatile("trap"); /* a program exception, in Dolphin too */
    }
    return 0;
}

static int32_t selftest_probe(const char* drive_root) {
    if (g_host != 0) g_host->log("probe of %s: no games of its own", drive_root);
    return 0;
}

void rwp_shutdown(void) {
    if (g_host != 0) g_host->log("shut down after %u init", g_inits);
}
