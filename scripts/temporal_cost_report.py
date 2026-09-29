"""Separate same-queue GPU temporal interval from the actual RR/NR product host."""
import math
from statistics import median


def summarize_temporal_cost(metrics, expected_frames, temporal):
    if temporal == 'off':
        return {'status': 'not_applicable', 'reason': 'temporal disabled'}
    if temporal == 'native-mv-refusal':
        return {'status': 'rejected', 'reason': 'PostRr native MV has no admitted motion proof',
                'recorded': 0, 'submitted': 0}
    rows = [row for row in metrics if isinstance(row.get('frame'), int)]
    result = {'status': 'insufficient', 'scope': 'actual Core PostRr temporal Record interval on one direct queue; excludes native RR, NR, private SR, and whole frame',
              'requested_outputs': expected_frames, 'stable_sample_n': 0,
              'excluded': {'creation_or_reset': 0, 'warmup': 0, 'diagnostic_capture': 0,
                           'not_recorded': 0, 'malformed': 0}}
    if not rows or [r.get('produced') for r in rows if r.get('produced', 0) > 0] != list(range(1, expected_frames + 1)):
        result['reason'] = 'missing or unordered complete outputs'
        return result
    samples = []
    memory = []
    previous = 0
    for row in rows:
        recorded, submitted = row.get('temporal_recorded'), row.get('temporal_submitted')
        if not isinstance(recorded, int) or not isinstance(submitted, int) or recorded < previous or submitted != recorded:
            result['excluded']['malformed'] += 1
            continue
        advanced = recorded > previous
        previous = recorded
        if row.get('nr_create') or row.get('produced', 0) == 0:
            result['excluded']['creation_or_reset'] += 1
        elif row['produced'] < 10:
            result['excluded']['warmup'] += 1
        elif row.get('diagnostic_capture'):
            result['excluded']['diagnostic_capture'] += 1
        elif not advanced:
            result['excluded']['not_recorded'] += 1
        elif isinstance(row.get('gpu_temporal_ms'), (int, float)) and math.isfinite(row['gpu_temporal_ms']) and row['gpu_temporal_ms'] > 0:
            samples.append(row['gpu_temporal_ms'])
            if isinstance(row.get('temporal_bytes'), int):
                memory.append(row['temporal_bytes'])
        else:
            result['excluded']['malformed'] += 1
    result['recorded'] = previous
    result['submitted'] = rows[-1].get('temporal_submitted')
    result['gpu_history_tested'] = rows[-1].get('temporal_gpu_tested')
    result['gpu_history_adopted'] = rows[-1].get('temporal_gpu_adopted')
    result['stable_sample_n'] = len(samples)
    if not isinstance(result['gpu_history_adopted'], int) or result['gpu_history_adopted'] <= 0:
        result['reason'] = 'no GPU pixel-history adoption receipt'
        return result
    if len(samples) < 16:
        result['reason'] = 'fewer than 16 completed stable temporal recordings'
        return result
    samples.sort()
    result.update(status='sufficient', gpu_temporal_ms={'median': median(samples),
        'p95': samples[math.ceil(.95 * len(samples)) - 1], 'sample_n': len(samples)})
    if len(memory) == len(samples):
        result['core_owned_temporal_bytes'] = {'sample_n': len(memory), 'median': median(memory),
            'maximum': max(memory), 'scope': 'Core temporal owner only; excludes opaque NGX and driver allocations'}
    return result
