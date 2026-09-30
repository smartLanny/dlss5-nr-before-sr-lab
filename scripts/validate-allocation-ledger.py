"""Exercise the production F8 allocation helper on CPU and a real WARP resource."""
import argparse
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/nr_allocation_ledger.h"
TEST = ROOT / "tests/allocation_ledger_test.cpp"
LOCK_TEST = ROOT / "tests/allocation_ledger_append_lock_test.cpp.in"


def check_worker_routing():
    capture = (ROOT / "src/nr_feedback_capture.inl").read_text(encoding="utf-8")
    core = (ROOT / "src/nr_feedback_core.inl").read_text(encoding="utf-8")
    if (capture.count("NrAppendAllocationLedger(") != 2 or
        core.count("NrAppendAllocationLedger(") != 2 or
        not (capture.index("DWORD WINAPI FeedbackWorker(") <
             capture.index('NrAppendAllocationLedger(*s,"start");') <
             capture.index('FeedbackAppendActivity(*s, "end");') <
             capture.index('NrAppendAllocationLedger(*s,"end");')) or
        not (core.index("DWORD WINAPI Worker(void *opaque)") <
             core.index('NrAppendAllocationLedger(s,"start");') <
             core.index('FeedbackAppendActivity(s, "end");') <
             core.index('NrAppendAllocationLedger(s,"end");'))):
        raise RuntimeError("allocation query must run only at start/end of both F8 workers")


def run(args, *, good=True):
    result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True)
    if (result.returncode == 0) != good:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        raise RuntimeError(f"unexpected exit {result.returncode}: {args}")
    return result


def build(compiler, include, exe):
    run([compiler, "-std=c++20", "-O0", "-Wall", "-Wextra", "-Werror", "-static",
         f"-I{include}", str(TEST), "-o", str(exe), "-ld3d12", "-ldxgi", "-ldxguid"])


def check_append_lock(compiler, out):
    runtime = (ROOT / "src/nr_allocation_ledger_runtime.inl").read_text(encoding="utf-8")
    marker = "void NrAppendAllocationLedger(FeedbackSession& session,const char* prefix) {"
    if runtime.count(marker) != 1:
        raise RuntimeError("production append function not uniquely found")
    function = runtime[runtime.index(marker):]
    acquire = "AcquireSRWLockExclusive(&g_feedback_lock);"
    release = "ReleaseSRWLockExclusive(&g_feedback_lock);"
    if (function.count(acquire) != 1 or function.count(release) != 1 or
        not (function.index("nr_allocation_runtime::Sample()") < function.index(acquire) <
             function.index("FormatSampleTicks") < function.index(release))):
        raise RuntimeError("resource sampling and report append lock order changed")
    template = LOCK_TEST.read_text(encoding="utf-8")
    for name, body, good in (("locked", function, True),
                             ("unlocked-control", function.replace(acquire, "").replace(release, ""), False)):
        source = out / f"append-{name}.cpp"
        exe = out / f"append-{name}.exe"
        source.write_text(template.replace("PRODUCTION_APPEND_FUNCTION", body), encoding="utf-8")
        run([compiler, "-std=c++20", "-O0", "-Wall", "-Wextra", "-Werror", "-static",
             f"-I{HEADER.parent}", str(source), "-o", str(exe)])
        observed = run([str(exe)], good=good)
        if not good and "FAIL production append" not in observed.stderr:
            raise RuntimeError("unlocked append control failed for an unrelated reason")
        (out / f"append-{name}.txt").write_text(observed.stdout + observed.stderr, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    check_worker_routing()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    exe = out / "allocation-ledger.exe"
    build(args.compiler, HEADER.parent, exe)
    success = run([str(exe), "--warp"])
    (out / "positive.txt").write_text(success.stdout, encoding="utf-8")
    check_append_lock(args.compiler, out)

    original = HEADER.read_text(encoding="utf-8")
    controls = {
        "borrowed": ("if(!identity||ownership==Ownership::Borrowed)return;",
                     "(void)ownership; if(!identity)return;"),
        "alias": ("if(!Claim(identity))return;", "(void)Claim(identity);"),
        "invalid-size": ("if(!bytes||bytes==std::numeric_limits<std::uint64_t>::max())",
                         "if(bytes==std::numeric_limits<std::uint64_t>::max())"),
        "overflow": ("value=std::numeric_limits<std::uint64_t>::max();overflow=true;",
                     "value+=increment;overflow=true;"),
        "move-phase": ("result_.cells[unsigned(category)][unsigned(phase)][unsigned(heap)];\n        SaturatingAdd(cell.bytes,bytes",
                       "result_.cells[unsigned(category)][unsigned(Phase::Live)][unsigned(heap)];\n        SaturatingAdd(cell.bytes,bytes"),
        "f8-units": ("GetResourceAllocationInfo.SizeInBytes; not resident",
                     "GetResourceAllocationInfo.SizeInBytes; resident"),
        "heap-kind": ('names[]={"default","readback","upload","other"};',
                      'names[]={"default","upload","readback","other"};'),
        "compat-retiring": ("if(!active)return;\n        result_.compat_retiring_unknown=true;",
                            "if(active)return;\n        result_.compat_retiring_unknown=true;"),
        "detached-quarantine": ("if(!uncertain)return;\n        result_.detached_quarantined_unknown=true;",
                                "if(uncertain)return;\n        result_.detached_quarantined_unknown=true;"),
        "sample-ticks": ("allocation.sampleStartedTickMs=", "allocation.sampleTickMs="),
    }
    receipts = [success.stdout.rstrip()]
    for name, (before, after) in controls.items():
        if original.count(before) != 1:
            raise RuntimeError(f"control anchor {name}: expected one match, got {original.count(before)}")
        directory = out / name
        directory.mkdir(exist_ok=True)
        (directory / HEADER.name).write_text(original.replace(before, after), encoding="utf-8")
        mutant = directory / "mutant.exe"
        build(args.compiler, directory, mutant)
        rejected = run([str(mutant), "--warp"], good=False)
        if "FAIL " not in rejected.stderr:
            raise RuntimeError(f"control {name} failed for an unrelated reason: {rejected.stderr}")
        receipts.append(f"{name}: {rejected.stderr.strip()}")
    (out / "controls.txt").write_text("\n".join(receipts) + "\n", encoding="utf-8")
    print("allocation ledger: CPU, WARP, worker routing, serialized production append, ten mutation controls PASS")


if __name__ == "__main__":
    main()
