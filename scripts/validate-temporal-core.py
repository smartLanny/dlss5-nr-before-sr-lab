#!/usr/bin/env python3
"""Run the complete generated Core's continuous temporal lifetime regression."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OLD_ORACLE = "CORE_TEMPORAL_OFF_DURING_CONTINUOUS_NR_ORACLE"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "reshade", "source", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--expect-old-runtime-failure", action="store_true",
                        help="Accept only the original frozen Core's exact continuous-OFF failure.")
    args = parser.parse_args()
    command = [sys.executable, str(ROOT / "scripts/validate-native-color-view.py"), "--temporal"]
    for name in ("compiler", "reshade", "source", "output"):
        command.extend(["--" + name, str(getattr(args, name).resolve())])
    result = subprocess.run(command, cwd=ROOT, check=False)
    output = args.output.resolve()
    receipt_path = output / "result.json"
    if not receipt_path.is_file():
        raise SystemExit(result.returncode or 1)
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    run_log = (output / "run.txt").read_text(encoding="utf-8")
    expected_failure = (args.expect_old_runtime_failure and result.returncode == 41
                        and receipt["run_exit"] == 41 and "FAIL " + OLD_ORACLE in run_log)
    passed = expected_failure if args.expect_old_runtime_failure else result.returncode == 0 and receipt["run_exit"] == 0
    record = {
        "schema": 1,
        "passed": passed,
        "command": command,
        "original_run_exit": receipt["run_exit"],
        "expected_old_runtime_failure": args.expect_old_runtime_failure,
        "negative_control_rejected": expected_failure,
        "expected_failure_oracle": OLD_ORACLE if args.expect_old_runtime_failure else None,
        "complete_core_receipt_sha256": hashlib.sha256(receipt_path.read_bytes()).hexdigest(),
        "run_log_sha256": hashlib.sha256((output / "run.txt").read_bytes()).hexdigest(),
        "contracts": [
            "current NR lease does not pin safely retired temporal resources",
            "real ApplyConfig OFF, Native/AMD, Whole/Later changes",
            "three command lists with actual Execute, Signal and native Reset",
            "completed fence without Reset remains replayable",
            "Reset with unfinished last replay fence retains resources",
            "replay retirement and same-configuration reconstruction",
            "master OFF maintenance without another temporal Prepare",
            "processing route leaves After without another temporal Prepare",
            "actual outer NR Reset gate quarantines discarded unsubmitted NR",
        ],
        "synthetic_nr": True,
        "warp_only": True,
        "game_verified": False,
    }
    (output / "temporal-regression.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    if expected_failure:
        print("PASS old generated Core rejected at " + OLD_ORACLE)
    raise SystemExit(0 if passed else result.returncode or 1)


if __name__ == "__main__":
    main()
