#!/usr/bin/env python3
"""Compare paired GPU samples. Refuses to label GEMM timings as complete NR gain."""
import argparse
import json
import math
import random
import statistics
from pathlib import Path

BOUNDARIES = {'ffn_ms', 'feature18_ms', 'nr_chain_ms', 'frame_gpu_ms', 'gemm_ms'}

def percentile(values, p):
    values = sorted(values)
    x = (len(values) - 1) * p
    lo = int(x)
    return values[lo] + (values[min(lo + 1, len(values)-1)] - values[lo]) * (x-lo)

def read(path, metric, require_pairs):
    rows = []
    for line in path.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if row.get('warmup', False):
            continue
        value = row.get(metric)
        if not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
            raise ValueError(f'{path}: missing/nonpositive/nonfinite {metric}')
        if row.get('valid', True) is not True or row.get('gpu_complete') is not True:
            raise ValueError(f'{path}: sample is invalid or GPU completion is unproven')
        if require_pairs and row.get('replaced_pairs', 0) < require_pairs:
            raise ValueError(f'{path}: required FFN replacements did not execute')
        if row.get('performance_context') == 'shared_gpu':
            raise ValueError(f'{path}: GPU contention disqualifies performance evidence')
        rows.append(row)
    if len(rows) < 2:
        raise ValueError('Need at least two non-warmup samples per run')
    return rows

def compare(baselines, candidates, metric, require_pairs=0):
    if metric not in BOUNDARIES or len(baselines) != len(candidates) or not baselines:
        raise ValueError('Pair baseline and candidate runs with an explicit timing boundary')
    blocks=[]
    baseline_values=[]
    candidate_values=[]
    for a,b in zip(baselines,candidates):
        ar=read(a,metric,0);br=read(b,metric,require_pairs)
        av=[r[metric] for r in ar];bv=[r[metric] for r in br]
        baseline_values.extend(av);candidate_values.extend(bv)
        am=statistics.median(av);bm=statistics.median(bv)
        blocks.append({'baseline':str(a),'candidate':str(b), 'baseline_samples':len(av),
                       'candidate_samples':len(bv),'baseline_median_ms':am,'candidate_median_ms':bm,
                       'saving_fraction':1-bm/am})
    # Resample complete A/B blocks, not correlated individual frames.
    savings=[b['saving_fraction'] for b in blocks]
    ci=None
    if len(blocks)>=5:
        rng=random.Random(1729)
        bootstrap=[statistics.mean(rng.choices(savings,k=len(savings))) for _ in range(10000)]
        ci=[percentile(bootstrap,.025),percentile(bootstrap,.975)]
    lower_positive=ci is not None and ci[0]>0
    sufficient=len(blocks)>=5 and all(min(b['baseline_samples'],b['candidate_samples'])>=600 for b in blocks)
    mean_saving=statistics.mean(savings)
    return {'metric':metric,'measurement_scope':metric.removesuffix('_ms'), 'paired_runs':blocks,
            'baseline_median_ms':statistics.median(baseline_values),'candidate_median_ms':statistics.median(candidate_values),
            'baseline_p95_ms':percentile(baseline_values,.95),'candidate_p95_ms':percentile(candidate_values,.95),
            'paired_mean_saving_fraction':mean_saving, 'run_block_bootstrap_95ci':ci,
            'minimum_sampling_met':sufficient,'statistically_positive':lower_positive,
            'performance_evidence_admissible':sufficient and lower_positive,
            'complete_nr_10_percent_target_met':metric=='nr_chain_ms' and sufficient and lower_positive and mean_saving>=.10,
            'quality_verified':False,'game_verified':False,
            'note':'Performance evidence alone does not qualify output quality or prove game FPS improvement.'}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,nargs='+',required=True)
    p.add_argument('--candidate',type=Path,nargs='+',required=True)
    p.add_argument('--metric',choices=sorted(BOUNDARIES),required=True)
    p.add_argument('--require-pairs',type=int,default=0)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();report=compare(a.baseline,a.candidate,a.metric,a.require_pairs)
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='paired_runs'},indent=2))

if __name__=='__main__':main()
