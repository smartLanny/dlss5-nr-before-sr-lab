#!/usr/bin/env python3
"""Read bounded corefix timing segments; never infer GPU or input latency."""
from __future__ import annotations
import argparse
import json
from pathlib import Path

BOUNDS_US = [100, 250, 500, 1000, 2000, 4000, 8000, 16000, 32000, 50000, 100000, None]
STAGES = ('carrier', 'chain-lock', 'queue-lock', 'submitted-fence-wait',
          'readback-map-copy', 'nr-evaluate', 'native-evaluate', 'native-execute', 'retirement-signal')

def quantile(histogram: list[int], percent: int) -> dict | None:
    count = sum(histogram)
    if not count:
        return None
    target = (count * percent + 99) // 100
    seen = 0
    for index, value in enumerate(histogram):
        seen += value
        if seen >= target:
            return {'lower_us': BOUNDS_US[index - 1] if index else 0,
                    'upper_us': BOUNDS_US[index], 'kind': 'histogram_bucket'}
    raise ValueError('Invalid histogram')

def summarize(text: str) -> dict:
    # Appended current/previous logs must not become current capture metadata.
    metadata = text.split('===== nr-current', 1)[0]
    fields = dict(line.split('=', 1) for line in metadata.splitlines() if '=' in line)
    def number(key: str) -> int:
        value = fields.get(key, '')
        if not value.isascii() or not value.isdecimal():
            raise ValueError(f'Missing or invalid {key}')
        return int(value)
    if fields.get('end.timing.schema') != '1' or fields.get('end.timing.available') != '1':
        raise ValueError('This report has no supported runtime timing snapshot')
    frequency = number('end.timing.qpcFrequency')
    started = number('end.timing.epochStartedTicks')
    captured = number('end.timing.capturedTicks')
    clock = fields.get('end.timing.clockAvailable') == '1' and frequency > 0 and captured >= started > 0
    stable = all(fields.get('start.timing.' + key) == fields.get('end.timing.' + key)
                 for key in ('epoch', 'probeMode', 'probePhase', 'waitMode', 'waitPhase'))
    mixed = fields.get('end.timing.mixedScopes') != '0'
    overflow = fields.get('end.timing.counterOverflow') != '0'
    result = {
        'schema': 'nr-cpu-timing-summary-v1', 'epoch': number('end.timing.epoch'),
        'probe_mode': number('end.timing.probeMode'), 'probe_phase': number('end.timing.probePhase'),
        'wait_mode': number('end.timing.waitMode'), 'wait_phase': number('end.timing.waitPhase'),
        'segment_seconds': (captured - started) / frequency if clock else None,
        'segment_stable_during_f8': stable, 'mixed_scopes': mixed, 'counter_overflow': overflow,
        'aggregate_scope': fields.get('end.timing.aggregateScope'),
        'aggregate_scope_available': fields.get('end.timing.aggregateScopeAvailable') == '1',
        'scoped_records': number('end.timing.scopedRecords'),
        'unscoped_records': number('end.timing.unscopedRecords'),
        'stages': {},
        'scope': 'Cumulative CPU stage observations since the segment began; nested stages overlap. '
                 'No GPU-time, FPS, full input latency, scene-equivalence or performance-pass claim.',
    }
    for stage in STAGES:
        raw = fields.get('end.timing.' + stage, '')
        values, separator, histogram_text = raw.partition(',histogram:')
        if not separator:
            raise ValueError(f'Missing histogram for {stage}')
        properties = dict(item.split(':', 1) for item in values.split(','))
        histogram = [int(value) for value in histogram_text.split(',')]
        count, measured = int(properties['count']), int(properties['measured'])
        total, maximum = int(properties['totalTicks']), int(properties['maxTicks'])
        if (len(histogram) != len(BOUNDS_US) or min(histogram + [count, measured, total, maximum]) < 0
                or measured > count or sum(histogram) != measured or maximum > total):
            raise ValueError(f'Inconsistent stage counters: {stage}')
        usable = clock and not overflow and measured > 0
        result['stages'][stage] = {
            'calls': count, 'measured_calls': measured,
            'mean_cpu_ms': total * 1000 / frequency / measured if usable else None,
            'segment_max_cpu_ms': maximum * 1000 / frequency if usable else None,
            'p95_cpu_us': quantile(histogram, 95) if usable else None,
            'p99_cpu_us': quantile(histogram, 99) if usable else None,
        }
    result['meets_segment_data_checks'] = bool(clock and stable and not mixed and not overflow
        and result['aggregate_scope_available'] and result['segment_seconds'] >= 30
        and result['stages']['carrier']['measured_calls'] > 0
        and result['stages']['nr-evaluate']['measured_calls'] > 0)
    return result

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reports', type=Path, nargs='+')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    results = [{'report': path.name, **summarize(path.read_text(encoding='utf-8-sig'))}
               for path in args.reports]
    output = json.dumps(results, ensure_ascii=False, indent=2) + '\n'
    if args.output:
        with args.output.open('x', encoding='utf-8') as stream:
            stream.write(output)
    else:
        print(output, end='')

if __name__ == '__main__':
    main()
