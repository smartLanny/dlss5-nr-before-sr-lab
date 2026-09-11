"""CPU contract for the default-picture reset transaction.

The production reset and overlay-apply function bodies are extracted into a
small lock/config fixture.  This exercises the actual ordering and rejection
boundaries without constructing a renderer, D3D device, game, or NR model.
"""

from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def definition(text: str, marker: str, semicolon: bool = False) -> str:
    start = text.index(marker)
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end + int(semicolon)]


HARNESS = r'''
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

enum class NrJitterMode : int { Legacy=0, GameInputPixel=1 };
CHAIN_CONFIG

std::recursive_mutex g_chain_mutex;
int g_chain_cs = 0;
std::atomic<unsigned> g_config_generation{1};
ChainConfig g_config{};
namespace nr_f8 { std::atomic<bool> g_config_hold{false}; }

enum class OverlayApplyResult { Applied, Busy, Conflict };

std::mutex g_state_mutex;
std::condition_variable g_state_cv;
std::atomic<bool> g_pause_after_core{false};
std::atomic<bool> g_release_core{false};
std::atomic<bool> g_core_written{false};
std::atomic<bool> g_force_noop{false};

void EnterCriticalSection(int*) {
    g_chain_mutex.lock();
    ++g_chain_cs;
}
void LeaveCriticalSection(int*) {
    if (g_chain_cs <= 0) throw std::runtime_error("unbalanced chain lock");
    --g_chain_cs;
    g_chain_mutex.unlock();
}

std::vector<std::pair<std::string, int>> g_ini_writes;
bool g_write_ok = true;
unsigned g_apply_locked_calls = 0;
bool g_last_rr = false;
unsigned g_last_percent = 0;

namespace nr_post_settings {
struct Options { bool rr=true; unsigned percent=100; };
std::atomic<unsigned> bits{256u | 100u};
std::atomic<unsigned long long> revision{0};
std::atomic<bool> ready{false}, persistence_failed{false}, request_rejected{false};
std::atomic<unsigned> rejected_percent{0};
constexpr bool RrPostAutomatic() { return true; }
void ApplyLocked(Options value) {
    if (value.percent < 50 || value.percent > 100)
        throw std::runtime_error("invalid post precision in reset");
    ++g_apply_locked_calls;
    g_last_rr = value.rr;
    g_last_percent = value.percent;
    bits.store((value.rr ? 256u : 0u) | value.percent,
               std::memory_order_release);
}
}

bool WriteIniInt(const wchar_t* key, int value) {
    const std::wstring wide(key ? key : L"");
    g_ini_writes.emplace_back(std::string(wide.begin(), wide.end()), value);
    return g_write_ok;
}

// The reset helper must call the real ApplyOverlayConfig boundary below.
bool ApplyConfig(ChainConfig next, const char*, bool = true) {
    if (g_force_noop.load(std::memory_order_acquire)) return false;
    g_config = next;
    g_config_generation.fetch_add(1, std::memory_order_release);
    g_core_written.store(true, std::memory_order_release);
    g_state_cv.notify_all();
    if (g_pause_after_core.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> wait_lock(g_state_mutex);
        g_state_cv.wait(wait_lock, [] {
            return g_release_core.load(std::memory_order_acquire);
        });
    }
    return true;
}

REAL_APPLY_OVERLAY
REAL_RESTORE
REAL_PICTURE_RESET

struct Snapshot {
    ChainConfig config;
    unsigned generation;
    unsigned post_percent;
    bool post_rr;
};

Snapshot CaptureSnapshot() {
    EnterCriticalSection(&g_chain_cs);
    const unsigned packed = nr_post_settings::bits.load(std::memory_order_acquire);
    Snapshot out{g_config, g_config_generation.load(std::memory_order_acquire),
                 packed & 255u, (packed & 256u) != 0};
    LeaveCriticalSection(&g_chain_cs);
    return out;
}

void ResetState(unsigned post_percent) {
    g_config = {};
    g_config_generation.store(1, std::memory_order_release);
    nr_f8::g_config_hold.store(false, std::memory_order_release);
    nr_post_settings::bits.store(256u | post_percent, std::memory_order_release);
    nr_post_settings::revision.store(0, std::memory_order_release);
    nr_post_settings::ready.store(false, std::memory_order_release);
    nr_post_settings::persistence_failed.store(false, std::memory_order_release);
    nr_post_settings::request_rejected.store(false, std::memory_order_release);
    g_ini_writes.clear();
    g_apply_locked_calls = 0;
    g_last_rr = false;
    g_last_percent = 0;
    g_write_ok = true;
    g_pause_after_core.store(false, std::memory_order_release);
    g_release_core.store(false, std::memory_order_release);
    g_core_written.store(false, std::memory_order_release);
    g_force_noop.store(false, std::memory_order_release);
}

int checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}

void CheckRestorePreservesContracts() {
    ChainConfig c{};
    c.enabled = false;
    c.mode = 1;
    c.identity_use_original = true;
    c.depth_mode = 1;
    c.r8_output_encoding = 2;
    c.jitter_mode = NrJitterMode::GameInputPixel;
    c.perf_epoch = 777;
    c.work_mode = 5;
    c.custom_work_scale = .75f;
    c.intensity = .5f;
    c.color_protection = false;
    RestoreRecommendedPictureSettings(c);
    Check(!c.enabled, "default reset re-enabled F6");
    Check(c.mode == 2, "default reset did not select NR before SR");
    Check(c.work_mode == 0 && c.custom_work_scale == 1.f,
          "default reset did not select exact N=R work");
    Check(c.intensity == 1.2f, "default reset did not select model strength 1.2");
    Check(c.color_protection, "default reset did not enable colour protection");
    Check(c.jitter_mode == NrJitterMode::Legacy,
          "default reset did not disable experimental NR jitter");
    Check(c.depth_mode == 1 && c.r8_output_encoding == 2 &&
          c.perf_epoch == 777,
          "default reset changed game compatibility contracts");
}

void CheckAppliedReset(unsigned old_percent, bool no_op) {
    ResetState(old_percent);
    ChainConfig next = g_config;
    next.enabled = false;
    next.work_mode = 0;
    next.custom_work_scale = 1.f;
    next.intensity = 1.2f;
    next.color_protection = true;
    const unsigned generation_before = g_config_generation.load();
    g_force_noop.store(no_op, std::memory_order_release);
    const auto result = ApplyPictureReset(next, 1);
    Check(result == OverlayApplyResult::Applied, "default reset was not applied");
    Check(!g_config.enabled && g_config.work_mode == 0 &&
          g_config.custom_work_scale == 1.f && g_config.intensity == 1.2f &&
          g_config.color_protection, "core reset did not preserve/apply defaults");
    const unsigned packed = nr_post_settings::bits.load(std::memory_order_acquire);
    Check((packed & 255u) == 100u && (packed & 256u) != 0,
          "applied reset did not commit RR/post 100 together");
    Check(g_apply_locked_calls == 1 && g_last_rr && g_last_percent == 100,
          "post reset was not applied exactly once");
    Check(!no_op || g_config_generation.load() == generation_before,
          "no-op core apply changed the core generation");
    Check(nr_post_settings::revision.load() == 1 &&
          nr_post_settings::ready.load() && !nr_post_settings::persistence_failed.load(),
          "post reset state markers are wrong");
    Check(g_ini_writes.size() == 2 && g_ini_writes[0] ==
          std::make_pair(std::string("RRPostEnabled"), 1) &&
          g_ini_writes[1] == std::make_pair(std::string("PostWorkPercent"), 100),
          "applied reset did not persist the two post keys in order");
}

void CheckRejectedReset(OverlayApplyResult expected, const char* label) {
    ResetState(75);
    ChainConfig before = g_config;
    if (expected == OverlayApplyResult::Busy)
        nr_f8::g_config_hold.store(true, std::memory_order_release);
    const auto result = ApplyPictureReset(g_config, expected == OverlayApplyResult::Conflict ? 0 : 1);
    Check(result == expected, label);
    Check(g_config.enabled == before.enabled && g_config.mode == before.mode &&
          g_config.work_mode == before.work_mode && g_config.intensity == before.intensity,
          "rejected reset changed core state");
    Check((nr_post_settings::bits.load() & 255u) == 75u &&
          g_apply_locked_calls == 0 && g_ini_writes.empty() &&
          nr_post_settings::revision.load() == 0,
          "rejected reset wrote post state");
}

void CheckCaptureSeesWholeTransaction() {
    ResetState(75);
    ChainConfig next = g_config;
    next.enabled = false;
    next.work_mode = 0;
    next.custom_work_scale = 1.f;
    next.intensity = 1.2f;
    next.color_protection = true;
    g_pause_after_core.store(true, std::memory_order_release);
    std::atomic<int> result_code{-1};
    std::thread reset([&] {
        result_code.store(static_cast<int>(ApplyPictureReset(next, 1)),
                          std::memory_order_release);
    });
    {
        std::unique_lock<std::mutex> wait_lock(g_state_mutex);
        g_state_cv.wait(wait_lock, [] {
            return g_core_written.load(std::memory_order_acquire);
        });
    }
    std::atomic<bool> capture_started{false}, capture_done{false};
    Snapshot captured{};
    std::thread capture([&] {
        capture_started.store(true, std::memory_order_release);
        // This is the real F8 ordering: claim the transient hold before
        // waiting for SnapshotConfig's chain lock.
        nr_f8::g_config_hold.store(true, std::memory_order_release);
        captured = CaptureSnapshot();
        nr_f8::g_config_hold.store(false, std::memory_order_release);
        capture_done.store(true, std::memory_order_release);
    });
    for (int i = 0; i < 100 && !capture_started.load(); ++i)
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Check(capture_started.load() && !capture_done.load(),
          "capture observed a half-committed reset");
    g_release_core.store(true, std::memory_order_release);
    g_state_cv.notify_all();
    reset.join();
    capture.join();
    Check(result_code.load() == static_cast<int>(OverlayApplyResult::Applied),
          "atomic reset did not complete");
        Check(captured.config.work_mode == 0 && captured.config.intensity == 1.2f &&
          captured.config.color_protection && captured.post_percent == 100 &&
          captured.post_rr, "capture did not see the complete reset transaction");
    Check(!nr_f8::g_config_hold.load(), "capture hold was not released");
}

void CheckOrdinaryEditKeepsPostPreference() {
    ResetState(75);
    ChainConfig edit = g_config;
    edit.intensity = 1.1f;
    const auto result = ApplyOverlayConfig(edit, 1);
    Check(result == OverlayApplyResult::Applied, "ordinary edit did not apply");
    Check(g_config.intensity == 1.1f, "ordinary edit did not update core state");
    Check((nr_post_settings::bits.load() & 255u) == 75u &&
          g_apply_locked_calls == 0 && g_ini_writes.empty(),
          "ordinary edit reset post preference");
}

int main() {
    try {
        CheckRestorePreservesContracts();
        for (unsigned old_percent : {87u, 75u}) {
            CheckAppliedReset(old_percent, false);
            CheckAppliedReset(old_percent, true);
        }
        CheckRejectedReset(OverlayApplyResult::Conflict, "generation conflict was not rejected");
        CheckRejectedReset(OverlayApplyResult::Busy, "capture busy reset was not rejected");
        CheckCaptureSeesWholeTransaction();
        CheckOrdinaryEditKeepsPostPreference();
        std::printf("picture reset: %d checks, 0 failures; CPU fixture only\n", checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL picture reset: %s\n", error.what());
        return 1;
    }
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "clang++"))
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise SystemExit(f"C++ compiler not found: {args.compiler}")

    core = (ROOT / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
    ui = (ROOT / "src/interop/nr_post_ui.inl").read_text(encoding="utf-8-sig")
    recommended = (ROOT / "src/nr_recommended_picture.inl").read_text(
        encoding="utf-8-sig")
    overlay = definition(core, "OverlayApplyResult ApplyOverlayConfig(")
    if "PostWorkPercent" in overlay or "nr_post_settings::ApplyLocked" in overlay:
        raise SystemExit("ordinary ApplyOverlayConfig owns post state")
    harness = HARNESS.replace(
        "CHAIN_CONFIG", definition(core, "struct ChainConfig", True))
    harness = harness.replace("REAL_APPLY_OVERLAY", overlay)
    harness = harness.replace(
        "REAL_RESTORE", definition(recommended, "void RestoreRecommendedPictureSettings("))
    harness = harness.replace(
        "REAL_PICTURE_RESET", definition(ui, "OverlayApplyResult ApplyPictureReset("))

    with tempfile.TemporaryDirectory(prefix="nr-picture-reset-") as directory:
        directory = Path(directory)
        source = directory / "picture_reset.cpp"
        executable = directory / ("picture_reset.exe" if os.name == "nt" else "picture_reset")
        source.write_text(harness, encoding="utf-8")
        command = [compiler, "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror"]
        if os.name == "nt":
            command.append("-static")
        subprocess.run(command + [str(source), "-o", str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True,
                                text=True, timeout=20)
        print(result.stdout, end="")
        print(result.stderr, end="")
        if result.returncode:
            raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
