#!/usr/bin/env python3
"""Focused Win32 INI migration/rejection checks; no GPU or NR execution."""

from pathlib import Path
import argparse
from contextlib import nullcontext
import importlib.util
import os
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "extract", ROOT / "scripts/extract-post-gpu.py")
EXTRACT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXTRACT)

HARNESS = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <iterator>
#include <limits>
#include "nr_work_config.h"
wchar_t g_ini_path[MAX_PATH] = {};
struct Config { int work_mode = 0; float custom_work_scale = 1; } g_config;
bool g_work_config_loaded = false;
std::atomic<bool> g_work_request_rejected{false};
std::atomic<bool> g_config_persistence_failed{false};
std::atomic<unsigned long> g_config_persistence_error{0};
void Log(const char *, ...) {}
HELPERS
unsigned checks = 0;
void Check(bool value, const char *message) {
    ++checks;
    if (!value) { std::printf("FAIL %s\n", message); std::exit(1); }
}
void ResetFile(const wchar_t *name) {
    wcscpy_s(g_ini_path, name);
    DeleteFileW(g_ini_path);
    g_work_config_loaded = false;
    g_config = {};
    g_config_persistence_failed = false;
    g_config_persistence_error = 0;
}
void Previous(int mode, float scale) {
    g_config = {mode, scale};
    g_work_config_loaded = true;
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 3) return 2;
    using namespace nr_work_config;
    ResetFile(argv[1]);
    Check(IsNewWorkConfigFile(), "absent file proven before writes");
    auto request = ReadWorkRequest(IsNewWorkConfigFile());
    Check(request.work.mode == 0 && request.work.scale == 1,
          "fresh install is exact Reference N=R");
    PersistWorkControl(request.work, false);
    Previous(request.work.mode, request.work.scale);
    request = ReadWorkRequest(false);
    Check(request.work.mode == 0 && request.work.scale == 1,
          "fresh defaults survive reload");

    ResetFile(argv[1]);
    WriteIniInt(L"Enabled", 1);
    Check(!IsNewWorkConfigFile(), "missing keys do not imply fresh install");
    request = ReadWorkRequest(false);
    Check(request.work.mode == 0 && request.work.scale == 1.f,
          "missing keys use the successor 100-percent default");
    for (int mode = 0; mode <= 5; ++mode) {
        for (float scale : {.5f, .67f, .75f, 1.f}) {
            WriteIniInt(L"WorkMode", mode);
            WriteIniFloat(L"CustomWorkScale", scale);
            request = ReadWorkRequest(false);
            Check(request.status == Status::Accepted &&
                      request.work.mode == mode && request.work.scale == scale,
                  "valid old setting retained exactly");
        }
    }

    WriteIniInt(L"WorkMode", 5);
    WriteIniFloat(L"CustomWorkScale", .25f);
    g_work_config_loaded = false;
    request = ReadWorkRequest(false);
    Check(request.status == Status::Rejected && request.work.mode == 0 &&
              request.work.scale == 1.f,
          "invalid initial custom uses declared 100-percent fallback");

    Previous(5, .67f);
    for (const wchar_t *invalid : {L"0.49", L"-1", L"1.01", L"nan", L"inf",
             L"", L"0.75junk", L"0.75%", L"garbage", L"1e-9999", L"1e9999"}) {
        WritePrivateProfileStringW(
            L"NRBeforeSR", L"CustomWorkScale", invalid, g_ini_path);
        request = ReadWorkRequest(false);
        Check(request.status == Status::Rejected && request.work.mode == 5 &&
                  request.work.scale == .67f,
              "invalid reload retains previous work pair");
        wchar_t retained[64] = {};
        GetPrivateProfileStringW(L"NRBeforeSR", L"CustomWorkScale", L"missing",
                                 retained, 64, g_ini_path);
        Check(std::wcscmp(retained, invalid) == 0,
              "rejection preserves invalid INI evidence");
    }
    for (const wchar_t *mode : {L"-1", L"6", L"1junk", L"",
                                L"999999999999999999999999"}) {
        WritePrivateProfileStringW(L"NRBeforeSR", L"WorkMode", mode, g_ini_path);
        WriteIniFloat(L"CustomWorkScale", .75f);
        request = ReadWorkRequest(false);
        Check(request.status == Status::Rejected && request.work.mode == 5 &&
                  request.work.scale == .67f,
              "invalid mode retains previous work pair");
    }

    WriteIniInt(L"WorkMode", 5);
    WriteIniFloat(L"CustomWorkScale", 0);
    request = ReadWorkRequest(false);
    Check(request.status == Status::Disable && request.work.scale == .67f,
          "explicit Custom zero keeps previous precision");
    PersistWorkControl(request.work, true);
    Check(GetPrivateProfileIntW(L"NRBeforeSR", L"Enabled", 1, g_ini_path) == 0,
          "explicit Custom zero disables master");
    request = Parse(L"1", L"0", {1, .75f});
    Check(request.status == Status::Rejected,
          "inactive zero cannot unexpectedly disable master");

    for (float bad : {-.1f, .49f, 1.1f,
                      std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity()}) {
        request = Resolve({5, bad}, {5, .75f});
        Check(request.status == Status::Rejected && request.work.scale == .75f,
              "numeric apply rejects rather than clamps");
    }

    wcscpy_s(g_ini_path, argv[2]);
    g_work_config_loaded = false;
    Check(!IsNewWorkConfigFile(), "unknown parent is not proven fresh");
    PersistWorkControl({0, 1}, false);
    Check(g_config_persistence_failed && g_config_persistence_error != 0,
          "write failure latches session-only state");
    std::printf("work config: %u focused Win32/CPU cases passed; GPU/NR calls 0\n",
                checks);
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument(
        "--build-dir", type=Path,
        help="Use an existing private build directory instead of system temp.")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("compiler not found")
    os.environ["PATH"] = (str(Path(compiler).resolve().parent) + os.pathsep +
                          os.environ.get("PATH", ""))
    source = (ROOT / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
    helpers = []
    for signature in ("bool WriteIniInt(", "bool WriteIniFloat("):
        helpers.append(EXTRACT.function(source[source.rindex(signature):], signature))
    for signature in ("bool IsNewWorkConfigFile(",
                      "void RecordWorkRequestStatus(",
                      "void PersistWorkControl(",
                      "nr_work_config::Request ReadWorkRequest("):
        helpers.append(EXTRACT.function(source, signature))
    build_context = (nullcontext(str(args.build_dir.resolve()))
                     if args.build_dir else
                     tempfile.TemporaryDirectory(prefix="nr-work-config-"))
    with build_context as directory:
        root = Path(directory)
        cpp = root / "test.cpp"
        exe = root / "test.exe"
        cpp.write_text(HARNESS.replace("HELPERS", "\n\n".join(helpers)),
                       encoding="utf-8")
        subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        "-municode", "-I" + str(ROOT / "src"), str(cpp),
                        "-o", str(exe)], check=True)
        subprocess.run([str(exe), str(root / "fixture.ini"),
                        str(root / "absent" / "fixture.ini")], check=True)

    reload_config = EXTRACT.function(source, "void ReloadConfig(")
    assert reload_config.index("IsNewWorkConfigFile()") < reload_config.index(
        "ReadConfiguredTransferStrength(")
    assert reload_config.index("ReadWorkRequest(") < reload_config.index(
        "ReadConfiguredTransferStrength(")
    apply_config = EXTRACT.function(source, "bool ApplyConfig(")
    assert (apply_config.index("EnterCriticalSection") <
            apply_config.index("nr_work_config::Resolve(") <
            apply_config.index("SameConfig("))
    normalize = EXTRACT.function(source, "void NormalizeConfig(")
    assert "config.work_mode =" not in normalize
    assert "config.custom_work_scale =" not in normalize
    config = (ROOT / "config/nr_before_sr.ini").read_text(encoding="utf-8-sig")
    assert "WorkMode=0" in config and "CustomWorkScale=1.00" in config
    print("Production load/apply ordering and fresh 100% defaults verified.")


if __name__ == "__main__":
    main()
