"""Cost summaries for the actual product-host timestamp receipt, not game FPS."""
import math
import re
from statistics import median

_FIELDS = re.compile(r'([A-Za-z_]\w*)=([0-9]+(?:\.[0-9]+)?)')
_MIN_STABLE = 16


def parse_metrics(log):
    return [{key: float(value) if '.' in value else int(value)
             for key, value in _FIELDS.findall(line)}
            for line in log.splitlines() if line.startswith('METRIC ')]


def _distribution(values):
    ordered = sorted(values)
    return {'sample_n': len(ordered), 'median': median(ordered),
            'p95': ordered[math.ceil(.95 * len(ordered)) - 1]}


def summarize_cost(metrics, expected_frames, layers, private):
    """Only complete output rows after matched warmup and before diagnostic capture.

    The host's private path creates on output 1 and infers on outputs 2 onward;
    outputs 2..9 are the eight production warmup inferences. The same output
    indices are excluded from ordinary controls for paired comparisons.
    """
    result = {'status': 'insufficient', 'scope': 'synthetic product-host GPU timestamp spans; not a game frame or full plugin cost',
              'minimum_stable_samples': _MIN_STABLE, 'requested_outputs': expected_frames,
              'excluded': {'creation_or_reset': 0, 'warmup': 0, 'diagnostic_capture': 0,
                           'incomplete_output': 0, 'malformed': 0}, 'stable_sample_n': 0,
              'gpu_stages_ms': {}, 'vram_bytes': {}}
    rows = [row for row in metrics if 'frame' in row]
    if not rows or not isinstance(expected_frames, int) or expected_frames < 2:
        result['reason'] = 'no complete frame metrics'
        return result
    required = ('frame', 'produced', 'nr_create', 'diagnostic_capture', 'private_enabled',
                'gpu_guides_ms', 'gpu_nr_chain_ms', 'gpu_private_sr_ms',
                'plane_bytes', 'dxgi_local_usage')
    complete = [row for row in rows if isinstance(row.get('produced'), int) and row['produced'] > 0]
    outputs = [row['produced'] for row in complete]
    if outputs != list(range(1, expected_frames + 1)):
        result['reason'] = 'missing, duplicate, or unordered completed outputs'
        result['observed_completed_outputs'] = outputs
        return result
    if any(row.get('private_enabled') != int(private) for row in rows):
        result['reason'] = 'mixed or missing private branch identity'
        return result
    eligible = []
    for row in rows:
        if not all(field in row for field in required) or row['private_enabled'] != int(private):
            result['excluded']['malformed'] += 1
        elif row['frame'] == 0 or row['nr_create']:
            result['excluded']['creation_or_reset'] += 1
        elif row['produced'] == 0:
            result['excluded']['incomplete_output'] += 1
        elif row['produced'] < 10:
            result['excluded']['warmup'] += 1
        elif row['diagnostic_capture']:
            result['excluded']['diagnostic_capture'] += 1
        elif all(isinstance(row[field], (int, float)) and math.isfinite(row[field]) and row[field] >= 0
                 for field in required[5:]):
            eligible.append(row)
        else:
            result['excluded']['malformed'] += 1
    result['stable_sample_n'] = len(eligible)
    if len(eligible) < _MIN_STABLE:
        result['reason'] = 'fewer than 16 stable completed outputs after exclusions'
        return result
    # Query 0..1 covers product Capture/guide normalization; query 2..3
    # covers NR chain; query 4..5 covers private Finish. Query 3..4 is the
    # separate temporal interval and is never counted as private cost. None covers
    # native Evaluate, scene, submission, readback, or the whole plugin.
    for key, applies in [('gpu_guides_ms', private or layers > 0),
                         ('gpu_nr_chain_ms', layers > 0),
                         ('gpu_private_sr_ms', private)]:
        result['gpu_stages_ms'][key] = _distribution([row[key] for row in eligible]) if applies else {'status': 'not_applicable'}
    result['vram_bytes'] = {
        'owned_private_planes': {**_distribution([row['plane_bytes'] for row in eligible]),
                                 'scope': 'Core-owned private plane accounting; excludes opaque NGX allocations'},
        'observed_dxgi_local_usage': {**_distribution([row['dxgi_local_usage'] for row in eligible]),
                                      'maximum': max(row['dxgi_local_usage'] for row in eligible),
                                      'scope': 'observed adapter process usage; not attributable to this feature alone'}}
    result['status'] = 'sufficient'
    return result


def compare_cost(ordinary, private):
    if ordinary['status'] != 'sufficient' or private['status'] != 'sufficient':
        return {'status': 'insufficient', 'reason': 'both controls require stable samples'}
    stages = {}
    for name in ('gpu_guides_ms', 'gpu_nr_chain_ms'):
        left = ordinary['gpu_stages_ms'][name]
        right = private['gpu_stages_ms'][name]
        if 'median' in left and 'median' in right:
            stages[name] = {'median_delta_ms': right['median'] - left['median'],
                            'p95_delta_ms': right['p95'] - left['p95'],
                            'ordinary_sample_n': left['sample_n'], 'private_sample_n': right['sample_n']}
    return {'status': 'sufficient', 'paired_stage_deltas': stages,
            'private_gpu_stage_ms': private['gpu_stages_ms']['gpu_private_sr_ms'],
            'scope': 'matched synthetic controls; separate stage spans, not summed frame or plugin cost'}
