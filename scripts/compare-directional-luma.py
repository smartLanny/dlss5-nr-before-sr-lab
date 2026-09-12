#!/usr/bin/env python3
"""Compare an isolated full signed-luma experiment to the actual D4 scalar.

Constructed scalar inputs only: no NVIDIA NR, images, GPU, HDR composition,
Magpie execution, material masks, or timing. Existing attenuation is supplied
as the same remaining edit to each implementation. No product files change.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASELINE_REV = "ec338d7d9d69230f208b25e4fd457cc79d03c465"

PREFIX = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
using std::abs; using std::isfinite;
static float min(float a,float b){return std::min(a,b);}
static float max(float a,float b){return std::max(a,b);}
static float clamp(float v,float a,float b){return std::clamp(v,a,b);}
static float saturate(float v){return std::clamp(v,0.f,1.f);}
static unsigned checks=0;
static void check(bool ok,const char *label) {
    ++checks; if(!ok){std::fprintf(stderr,"DIRECTION_ORACLE: %s\n",label);std::exit(23);}
}
static bool equal_bits(float a,float b){return std::memcmp(&a,&b,sizeof(float))==0;}
static bool close(float a,double b){return std::abs(double(a)-b)<=2e-6*std::max(1.,std::abs(b));}
'''

TESTS = r'''
int main(){
    using nr_experiment::DirectionalLumaStrength;
    // Independent expectations have no dependency on the D4 partition helper.
    for(float edit:{-.5f,.5f}) for(float fraction:{0.f,.25f,.5f,.75f,1.f})
    for(float attenuation:{0.f,.25f,.75f,1.f}) for(float master:{0.f,.5f,1.f,2.f,4.f}) {
        const float remaining=edit*attenuation;
        const float d4neutral=LightStyleStrengthV1(remaining,edit,edit*fraction,master,1,1,1,1,0);
        const float neutral=DirectionalLumaStrength(edit,remaining,master,1,1);
        check(equal_bits(d4neutral,neutral),"neutral exact D4 strength for admitted finite inputs");
        for(float gain:{0.f,.5f,1.f,2.f}) {
            const float full=DirectionalLumaStrength(edit,remaining,master,gain,gain);
            const double expected=attenuation==0.f && gain!=1.f ? 0. : std::min(4.,double(master)*gain);
            check(close(full,expected),"full signed direction independent analytic gain");
            check(remaining==0.f || remaining*(remaining*full)>=0.f,"no sign reversal");
            check(remaining!=0.f || remaining*full==0.f,"cannot resurrect removed edit");
            check(isfinite(full)&&full>=0.f&&full<=4.f,"finite public cap");
            const float unrelated=edit>0.f ?
                DirectionalLumaStrength(edit,remaining,master,gain,0.f) :
                DirectionalLumaStrength(edit,remaining,master,0.f,gain);
            // Neutral zero remainder may retain strength, but not its edit.
            check(equal_bits(remaining*full,remaining*unrelated),"opposite direction isolated");
        }
    }
    check(DirectionalLumaStrength(.5f,.5f,4,2,1)==4.f,"effective strength cap cannot be bypassed");
    check(DirectionalLumaStrength(.5f,-.1f,1,2,1)==0.f,"opposite remainder cannot be amplified");
    check(DirectionalLumaStrength(0,.1f,1,2,1)==0.f,"zero original cannot create an edit");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();
    for(float invalid:{nan,inf}) {
        check(DirectionalLumaStrength(invalid,.2f,1,2,1)==0.f,"invalid edit rejected");
        check(DirectionalLumaStrength(.2f,invalid,1,2,1)==0.f,"invalid remainder rejected");
        check(DirectionalLumaStrength(.2f,.2f,invalid,2,1)==0.f,"invalid master rejected");
        check(DirectionalLumaStrength(.2f,.2f,1,invalid,1)==0.f,"invalid gain rejected");
    }
    // Counterexamples distinguish complete-category scope from D4 fine-only.
    check(LightStyleStrengthV1(.5f,.5f,.5f,1,1,1,2,1,0)==1.f,"D4 wide bright unaffected by reflection");
    check(DirectionalLumaStrength(.5f,.5f,1,2,1)==2.f,"full direction reaches wide bright edit");
    check(close(LightStyleStrengthV1(.5f,.5f,.25f,1,1,1,2,1,0),1.5),"D4 mixed control gain");
    check(DirectionalLumaStrength(-.5f,-.5f,1,2,1)==1.f,"positive gain retains negative edit");
    std::printf("PASS %u numeric assertions\n",checks);
    // Machine rows hold all other parameters fixed: master1, both direction
    // gains2, broad gains1, glow0, and a supplied 75% protection remainder.
    for(float edit:{-.5f,.5f}) for(float fraction:{0.f,.5f,1.f}) {
        const float remaining=.25f*edit;
        const float old=LightStyleStrengthV1(remaining,edit,edit*fraction,1,1,1,2,2,0);
        const float full=DirectionalLumaStrength(edit,remaining,1,2,2);
        std::printf("ROW %.9g %.9g %.9g %.9g %.9g %.9g\n",edit,fraction,remaining,old,full,remaining*full);
    }
}
'''


def git(*args: str) -> str:
    return subprocess.check_output(["git", "-C", str(ROOT), *args], encoding="utf-8").strip()


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("Output exists; preserve previous evidence.")
    source_bytes = (ROOT / "src/nr_before_sr.cpp").read_bytes()
    source = source_bytes.decode("utf-8-sig").replace("\r\n", "\n")
    baseline = git("show", BASELINE_REV + ":src/nr_before_sr.cpp").replace("\r\n", "\n")
    pattern = r"// FOUR_REGION_SCALAR_BEGIN\n(.*?)// FOUR_REGION_SCALAR_END"
    current_match = re.search(pattern, source, re.S)
    baseline_match = re.search(pattern, baseline, re.S)
    if not current_match or not baseline_match or current_match.group(1) != baseline_match.group(1):
        raise ValueError("Current D4 scalar differs from the pinned comparison baseline.")
    helper = current_match.group(1)
    candidate_path = ROOT / "experiments/directional_luma.h"
    candidate_bytes = candidate_path.read_bytes()
    candidate = candidate_bytes.decode("utf-8").replace("#pragma once", "")
    mutation = "std::clamp(master * std::clamp(direction, 0.f, 2.f), 0.f, 4.f)"
    if candidate.count(mutation) != 1:
        raise ValueError("Negative control must mutate exactly one cap.")
    variants = [("candidate", candidate),
                ("missing_cap", candidate.replace(mutation, "master * std::clamp(direction, 0.f, 2.f)")),
                ("no_response", candidate.replace("const float direction = original_edit > 0.f ? positive : negative;",
                                                  "const float direction = 1.f;"))]
    runs = []
    rows = []
    with tempfile.TemporaryDirectory(prefix="nr-directional-experiment-") as directory:
        for name, code in variants:
            cpp = Path(directory) / (name + ".cpp")
            exe = cpp.with_suffix(".exe" if os.name == "nt" else "")
            cpp.write_text(PREFIX + helper + code + TESTS, encoding="utf-8")
            subprocess.run([args.compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                            str(cpp), "-o", str(exe)], check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            if name == "candidate":
                if run.returncode != 0:
                    raise RuntimeError(run.stdout + run.stderr)
                for line in run.stdout.splitlines():
                    if line.startswith("ROW "):
                        values = [float(x) for x in line.split()[1:]]
                        rows.append(dict(zip(("original_edit", "broad_fraction", "remaining_edit",
                                              "d4_gain", "full_direction_gain", "full_direction_edit"), values)))
            elif run.returncode != 23 or "DIRECTION_ORACLE:" not in run.stderr:
                raise RuntimeError(f"Negative control {name} failed for an unexpected reason: {run}")
            runs.append({"name": name, "returncode": run.returncode,
                         "stdout": run.stdout, "stderr": run.stderr})
    result = {
        "schema": "nr-directional-luma-experiment-v1", "success": True,
        "repository_head": git("rev-parse", "HEAD"),
        "worktree_dirty": bool(git("status", "--porcelain")),
        "d4_baseline_commit": BASELINE_REV,
        "source_sha256": digest(source_bytes), "scalar_sha256": digest(helper.encode()),
        "candidate_sha256": digest(candidate_bytes),
        "runner_sha256": digest(Path(__file__).read_bytes()),
        "compiler": subprocess.check_output([args.compiler, "--version"], text=True).splitlines()[0],
        "input_kind": "constructed scalar log edits, supplied common attenuation",
        "gpu_or_game_verified": False, "hdr_composition_verified": False,
        "magpie_executed": False, "runtime_changed": False,
        "limitations": ["Full direction also amplifies broad glow; no material or glow classification.",
                        "Existing attenuation is held fixed, not derived from image planes here.",
                        "Full direction is an isolated luma experiment, not Magpie RGB/HSL emulation."],
        "runs": runs, "rows": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as out:
        json.dump(result, out, indent=2, ensure_ascii=False)
        out.write("\n")
    print(runs[0]["stdout"].splitlines()[0])
    print("PASS two compiled negative controls; six fixed-input comparison rows")


if __name__ == "__main__":
    main()
