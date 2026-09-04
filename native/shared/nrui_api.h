// SPDX-License-Identifier: MIT
// Versioned x64 C ABI. No STL objects, ImGui pointers or game resources cross it.
#pragma once
#include <stdint.h>
#if defined(_WIN32)
#define NRUI_CALL __cdecl
#else
#define NRUI_CALL
#endif
enum NrUiResult { NRUI_OK=0, NRUI_BUSY=1, NRUI_CONFLICT=2, NRUI_UNSUPPORTED=3, NRUI_INVALID=4, NRUI_IO_ERROR=5 };
enum NrUiKey { NRUI_ENABLED=1, NRUI_MODE=2, NRUI_STRENGTH=3, NRUI_STYLE=4, NRUI_MASK=5, NRUI_WORK_MODE=6 };
struct NrUiStateV1 {
    uint32_t size, version, generation, enabled;
    uint32_t mode, style, auto_mask, work_mode;
    float strength, custom_work_scale;
    uint32_t bypass_reason, capture_busy;
    uint64_t submitted_count;
};
static_assert(sizeof(NrUiStateV1)==56, "NRUI ABI layout changed");
using NrUiGetV1 = uint32_t (NRUI_CALL *)(NrUiStateV1 *);
using NrUiSetV1 = uint32_t (NRUI_CALL *)(uint32_t key, float value, uint32_t expected_generation);
