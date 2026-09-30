#!/usr/bin/env python3
"""Compile the actual F8/F7 Present dispatch blocks with deterministic key seams."""
from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def function(text: str, signature: str) -> str:
    start = text.index(signature)
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def statement_range(body: str, first: str, last: str) -> str:
    start = body.index(first)
    end = body.index(last, start) + len(last)
    return body[start:end]


HARNESS = r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include "nr_hotkeys.h"

SHORT key_state[256] = {};
SHORT TestGetAsyncKeyState(int key) { return key_state[key & 255]; }
#define GetAsyncKeyState TestGetAsyncKeyState

namespace reshade::api {
enum class input_source { keyboard };
struct effect_runtime {
    bool overlay_open = false;
    void open_overlay(bool open, input_source) { overlay_open = open; }
};
}
namespace nr_f8 {
enum class Comparison { ShareMatrix, Current };
struct Options { Comparison comparison; bool custom; unsigned x, y; };
std::atomic<bool> g_settings_feedback_page_requested{false};
bool active = false;
unsigned core_requests = 0, current_requests = 0;
bool RequestCore() { ++core_requests; return true; }
bool RequestFullscreen(Comparison comparison=Comparison::Current) {
    if (comparison == Comparison::Current) ++current_requests;
    return true;
}
bool IsActive() { return active; }
PAGE_FUNCTIONS
}
void Log(const char *, ...) {}

void Dispatch(reshade::api::effect_runtime *runtime, ULONGLONG now_ms) {
    static nr_hotkeys::Edge image_edge;
    static ULONGLONG last_stage_request_ms = 0;
F8_BLOCK
F7_BLOCK
}

void Require(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}
int main() {
    reshade::api::effect_runtime runtime;
    Dispatch(&runtime, 900); // A newly published binding arms only after release.
    key_state[VK_F8] = static_cast<SHORT>(0x8001u);
    Dispatch(&runtime, 1000);
    Require(nr_f8::core_requests == 1,
            "F8 physical edge did not request one TXT and settings page");
    Require(nr_f8::TakeSettingsFeedbackPageRequest(),
            "F8 page request did not survive until DrawSettings consumption");
    Require(!nr_f8::TakeSettingsFeedbackPageRequest(),
            "F8 page request was consumed more than once");
    Require(runtime.overlay_open, "closed settings overlay was not opened after F8 request");
    runtime.overlay_open = false;
    for (unsigned i = 0; i < 128; ++i) {
        key_state[VK_F8] = static_cast<SHORT>((i & 1u) ? 0x8000u : 0x8001u);
        Dispatch(&runtime, 1001 + i);
    }
    Require(nr_f8::core_requests == 1, "held F8 repeated core requests");
    key_state[VK_F8] = 0; Dispatch(&runtime, 1200);
    key_state[VK_F8] = 1; Dispatch(&runtime, 1201);
    Require(nr_f8::core_requests == 2, "short F8 tap between Presents was lost");

    key_state[VK_F8] = 0;
    key_state[VK_F7] = static_cast<SHORT>(0x8001u);
    runtime.overlay_open = false; Dispatch(&runtime, 2000);
    Require(nr_f8::current_requests == 1 && runtime.overlay_open,
            "F7 did not request Current supplement while overlay was closed");
    Require(nr_f8::TakeSettingsFeedbackPageRequest() &&
                !nr_f8::TakeSettingsFeedbackPageRequest(),
            "F7 page request was not retained for exactly one consumer");
    for (unsigned i = 0; i < 128; ++i) {
        key_state[VK_F7] = static_cast<SHORT>((i & 1u) ? 0x8000u : 0x8001u);
        Dispatch(&runtime, 2001 + i);
    }
    Require(nr_f8::current_requests == 1, "held F7 repeated image requests");
    key_state[VK_F7] = 0; Dispatch(&runtime, 2200);
    key_state[VK_F7] = 1; Dispatch(&runtime, 2400);
    Require(nr_f8::current_requests == 2, "short F7 tap between Presents was lost");
    key_state[VK_F7] = 0; Dispatch(&runtime, 2500);
    nr_f8::active = true;
    key_state[VK_F7] = static_cast<SHORT>(0x8001u); Dispatch(&runtime, 3000);
    Require(nr_f8::current_requests == 2, "busy F7 started an overlapping request");
    std::puts("production F8/F7 Present dispatch: edge, hold, short tap, busy and closed-overlay PASS");
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    args = parser.parse_args()
    source = (ROOT / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
    present = function(source, "void OnReShadePresent(")
    f8 = statement_range(present, "const auto shortcut_bindings=", "runtime->open_overlay(true,reshade::api::input_source::keyboard);\n    }")
    f7 = statement_range(present, "const auto image_key=", 'accepted ? "accepted" : "busy");\n    }')
    assert "#if" not in present and "#ifdef" not in present, "hotkeys are preprocessor-gated"
    assert f8.index("nr_f8::RequestCore();") < f8.index("runtime->open_overlay("), "F8 request must precede UI"
    assert "nr_f8::RequestFullscreen()" in f7, "F7 hotkey must request the default Current capture"
    assert present.index("nr_core_feedback::Present(runtime);") < present.index("PollReportPathClipboard("), "clipboard must follow collector progress"
    assert "nr_f8::Present(runtime);" in present, "F7 must progress from Present even without visible UI"
    settings = (ROOT / "src/nr_unified_pages.inl").read_text(encoding="utf-8-sig")
    assert "RequestFullscreen()" in settings and "Comparison::ShareMatrix" in settings, "both F7 actions must remain in settings"
    assert "NR_HAS_F8_SEQUENCE" not in settings, "undefined feature macro hides F8/F7 from production settings"
    sequence = (ROOT / "src/nr_f8_sequence.inl").read_text(encoding="utf-8-sig")
    assert "bool RequestFullscreen(Comparison kind = Comparison::Current)" in sequence, "default F7 action must remain Current"
    assert "bool TakeSettingsFeedbackPageRequest()" in sequence, "main settings page request seam missing"
    assert "DLSS5 feedback capture###dlss5-feedback-window" not in sequence, "independent feedback window returned"
    dllmain = function(source, "BOOL APIENTRY DllMain(")
    assert "register_event<reshade::addon_event::reshade_present>" in dllmain and "OnReShadePresent" in dllmain
    assert "#if" not in dllmain and "#ifdef" not in dllmain, "event registration differs by language/debug preprocessor"
    build = (ROOT / "scripts/build.ps1").read_text(encoding="utf-8-sig")
    assert "'-DNDEBUG'" in build and build.count("NR_UI_CHINESE=") >= 2, "both Release language builds are required"

    page_functions = "\n".join((
        function(sequence, "void RequestSettingsFeedbackPage("),
        function(sequence, "bool TakeSettingsFeedbackPageRequest("),
        function(sequence, "void OpenWindow("),
    ))
    cpp_text = (HARNESS.replace("F8_BLOCK", f8).replace("F7_BLOCK", f7)
                .replace("PAGE_FUNCTIONS", page_functions))
    with tempfile.TemporaryDirectory(prefix="nr-feedback-hotkeys-") as temporary:
        cpp = Path(temporary) / "hotkeys.cpp"
        exe = Path(temporary) / "hotkeys.exe"
        cpp.write_text(cpp_text, encoding="utf-8")
        subprocess.run([args.compiler, "-std=c++17", "-O2", "-static", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "src"), str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print("Release/source wiring: bilingual common callback, F8 TXT, F7 Current+ShareMatrix, Present progress and clipboard poll PASS")


if __name__ == "__main__":
    main()
