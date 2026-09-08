#!/usr/bin/env python3
"""CPU-only candidate dimensions for a separately authorized NR size experiment.

Uses the current production alignment and percent policy. This tool neither
enables 1..49% in the product nor infers model admission from alignment.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('post_extract', ROOT/'scripts/extract-post-gpu.py')
extract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extract)

HARNESS = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include "nr_precision_ui.h"
#include "interop/nr_post_compat.h"
ALIGNMENT
int main(int argc, char** argv) {
    if (argc != 5) return 2;
    const unsigned w = std::strtoul(argv[1], nullptr, 10);
    const unsigned h = std::strtoul(argv[2], nullptr, 10);
    const unsigned p = std::strtoul(argv[3], nullptr, 10);
    const bool post = std::strtoul(argv[4], nullptr, 10) != 0;
    if (!w || !h || w > 32768 || h > 32768 || p > 100) return 3;
    const auto front = nr_precision_ui::Resolve(static_cast<float>(p), true);
    const bool accepted = post ? (p == 0 || nr_post_compat::SupportedPercent(p)) : front.accepted;
    if (p == 0) { std::printf("%u 1 0 0 0 0\n", accepted); return 0; }
    unsigned dw=w, dh=h, nw=w, nh=h;
    if (p != 100) {
        // Same route-specific rounding as ResolveNrWorkExtent / post Work.
        // For p<50 these are candidate calculations, never a product request.
        if (post) {
            dw=std::max(1u,static_cast<unsigned>((std::uint64_t(w)*p+50)/100));
            dh=std::max(1u,static_cast<unsigned>((std::uint64_t(h)*p+50)/100));
        } else {
            const float scale=static_cast<float>(p)*0.01f;
            dw=static_cast<unsigned>(std::floor(w*scale+0.5f));
            dh=static_cast<unsigned>(std::floor(h*scale+0.5f));
        }
        SelectAlignedWorkExtent(dw,dh,w,h,nw,nh);
    }
    if (post && p >= 50) {
        const auto actual=nr_post_compat::Work({w,h},p);
        if (actual.work.w!=dw || actual.work.h!=dh) return 4;
    }
    std::printf("%u 0 %u %u %u %u\n",accepted,dw,dh,nw,nh);
}
'''


def build(directory: Path, compiler: str) -> Path:
    source = (ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    alignment = '\n\n'.join(extract.function(source, signature) for signature in (
        'unsigned int AlignWorkExtent(', 'void SelectAlignedWorkExtent('))
    cpp = directory/'planner.cpp'
    cpp.write_text(HARNESS.replace('ALIGNMENT', alignment), encoding='utf-8')
    exe = directory/('planner.exe' if os.name == 'nt' else 'planner')
    subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT/'src'), str(cpp), '-o', str(exe)], check=True)
    return exe


def plan(exe: Path, width: int, height: int, percent: int, route: str) -> dict:
    values = subprocess.check_output([str(exe), str(width), str(height), str(percent),
                                      str(int(route == 'after'))], text=True).split()
    admitted, disabled, dw, dh, nw, nh = map(int, values)
    candidate = None if disabled else [nw, nh]
    reason = ('master_off_keep_saved_precision' if disabled else
              'below_current_sampling_policy_and_model_size_unverified' if not admitted else
              'current_percent_policy_only_runtime_admission_still_required')
    return dict(route=route, percent=percent, reference_extent=[width, height],
                reference_semantics='O' if route == 'after' else 'R',
                rounded_request=None if disabled else [dw, dh], candidate_work=candidate,
                aligned_linear_percent=None if disabled else
                    [100*nw/width, 100*nh/height],
                aligned_pixel_percent=None if disabled else 100*nw*nh/(width*height),
                current_percent_policy_admits=bool(admitted), disabled=bool(disabled),
                rejection_or_boundary=reason, model_size_admission='unknown',
                executable_product_range_changed=False,
                experimental_only=bool(not admitted and not disabled))


def check(exe: Path) -> int:
    checks = 0
    for w,h in ((1,1),(7,5),(31,23),(427,240),(640,360),(1920,1080),(3840,2160)):
        for route in ('before','after'):
            for percent in (0,1,4,12,25,33,49,50,67,75,99,100):
                row=plan(exe,w,h,percent,route)
                assert row['current_percent_policy_admits'] == (percent==0 or percent>=50)
                assert row['model_size_admission']=='unknown'
                if percent==0:
                    assert row['disabled'] and row['candidate_work'] is None
                else:
                    nw,nh=row['candidate_work']
                    assert 1<=nw<=w and 1<=nh<=h
                    if percent==100: assert (nw,nh)==(w,h)
                    if percent<50: assert row['experimental_only']
                checks+=1
    # Demonstrates that rejected requests are reported at their own candidate
    # dimensions, rather than being silently substituted with a 50% plan.
    assert plan(exe,640,360,1,'before')['candidate_work'] != plan(exe,640,360,50,'before')['candidate_work']
    # A successful odd-width 1:1 probe cannot imply a global 32-pixel minimum
    # or alignment restriction; the production reference is exactly preserved.
    assert plan(exe,427,240,100,'before')['candidate_work']==[427,240]
    return checks+2


def main() -> None:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler', default=os.environ.get('CXX','g++'))
    ap.add_argument('--width', type=int, default=640)
    ap.add_argument('--height', type=int, default=360)
    ap.add_argument('--percent', type=int, nargs='+', default=[0,100,75,67,50,49,40,33,25,16,12,8,4,2,1])
    ap.add_argument('--route', choices=['before','after','both'], default='both')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--self-test', action='store_true')
    args=ap.parse_args()
    compiler=shutil.which(args.compiler)
    if not compiler: ap.error('C++ compiler not found')
    # llvm-mingw's host executable needs its adjacent C++ runtime DLLs. This
    # process-local PATH affects only the CPU planner, never the user's setup.
    os.environ['PATH']=str(Path(compiler).resolve().parent)+os.pathsep+os.environ.get('PATH','')
    if not (1<=args.width<=32768 and 1<=args.height<=32768):
        ap.error('planner extent must be 1..32768; this is a host bound, not a model capability')
    if len(args.percent)>101 or any(p<0 or p>100 for p in args.percent):
        ap.error('provide at most 101 percentages, each 0..100')
    if args.output.exists(): ap.error('output already exists; preserve prior evidence and use a new file')
    routes=('before','after') if args.route=='both' else (args.route,)
    with tempfile.TemporaryDirectory(prefix='nr-size-plan-') as directory:
        exe=build(Path(directory),args.compiler)
        checks=check(exe) if args.self_test else 0
        rows=[plan(exe,args.width,args.height,p,route) for route in routes for p in args.percent]
    files=('src/nr_before_sr.cpp','src/nr_precision_ui.h','src/interop/nr_post_compat.h',
           'scripts/extract-post-gpu.py','scripts/plan-nr-size-admission.py')
    report=dict(schema='nr-size-admission-plan-v1',
                source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                source_dirty=bool(subprocess.check_output(['git','status','--porcelain','--untracked-files=all'],cwd=ROOT,text=True).strip()),
                source_files_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in files},
                cpu_contract_checks=checks, gpu_calls=0, nr_calls=0,
                model_minimum_extent=None, model_alignment_requirement=None,
                observed_runtime_evidence_imported=False, rows=rows)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'CPU planner: {len(rows)} candidates, {checks} checks; GPU/NR calls 0; model minimum unknown.')


if __name__=='__main__': main()
