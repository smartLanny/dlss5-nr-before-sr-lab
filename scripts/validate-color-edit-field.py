#!/usr/bin/env python3
"""CPU-only #86 colour-field contract; no D3D device or model call."""
import math
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LUMA = (0.2126, 0.7152, 0.0722)
EPS = 1.0 / 4096.0


def dot(a, b=LUMA):
    return sum(x * y for x, y in zip(a, b))


def build(proxy, neural):
    values = (*proxy, *neural)
    if not all(math.isfinite(x) and abs(x) <= 65504.0 for x in values):
        return (0.0, 0.0, 0.0, 0)
    py, ny = dot(proxy), dot(neural)
    edit = max(-8.0, min(8.0, math.log2(max(ny, EPS) / max(py, EPS))))
    if py < EPS or ny < EPS or min(values) < 0.0:
        return (edit, 0.0, 0.0, 1)
    pn = tuple(x / py for x in proxy)
    nn = tuple(x / ny for x in neural)
    delta = ((nn[0] - nn[1]) - (pn[0] - pn[1]),
             (nn[2] - nn[1]) - (pn[2] - pn[1]))
    assert max(map(abs, delta)) <= 32.0
    return (edit, delta[0], delta[1], 3)


def rgb_delta(opponent):
    dg = -LUMA[0] * opponent[0] - LUMA[2] * opponent[1]
    return (dg + opponent[0], dg, dg + opponent[1])


def ratio_limit(base, delta, ratio):
    limit = 2.0
    for b, d in zip(base, delta):
        if d > 0.0:
            limit = min(limit, max(b * (ratio - 1.0), 0.0) / d)
        elif d < 0.0:
            limit = min(limit, max(b * (1.0 - 1.0 / ratio), 0.0) / -d)
    return max(limit, 0.0)


def wiring():
    source = (ROOT / 'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    assert 'bool low_grid_color_edit_field = false;' in source
    assert 'L"LowGridColorEditField", 0, g_ini_path' in source
    assert 'WriteIniInt(L"LowGridColorEditField", config.low_grid_color_edit_field ? 1 : 0)' in source
    history = source.split('bool SameHistoryConfig(', 1)[1].split('\n}', 1)[0]
    assert 'low_grid_color_edit_field' not in history
    assert 'float2 ReadLumaEditV6(int2 p)' in source
    assert 'DXGI_FORMAT_R32G32B32A32_FLOAT' in source
    assert 'CanMutateSlotLocked(slot)' in source
    assert 'allocation.SizeInBytes > kEditFieldSlotBudget - slot.edit_field_bytes' in source
    assert 'allocation.SizeInBytes > kEditFieldTotalBudget - g_edit_field_allocated_bytes' in source
    assert 'ReleaseCom(slot.edit_field);' in source
    assert '(low_grid_color ? 32768u : 0u)' in source
    build_shader = source.split('void BuildColorEditFieldCS(', 1)[1].split(
        'float2 ReadLumaEditV6', 1)[0]
    for forbidden in ('OriginalColor', 'EdgeGuard', 'LightingLock',
                      'DetailStability', 'TransferStrength'):
        assert forbidden not in build_shader
    assert 'float3 logRgbRatio = log2(' in source  # frozen legacy reference remains


def numeric():
    assert build((1.0, 1.0, 1.0), (2.0, 2.0, 2.0))[3] == 3
    assert build((0.0, 0.0, 0.0), (1e-8, 1e-8, 1e-8))[3] == 1
    assert build((math.nan, 0.0, 0.0), (1.0, 1.0, 1.0))[3] == 0
    assert build((1000.0, 20.0, 2.0), (2.0, 20.0, 1000.0))[3] == 3
    rng = random.Random(86)
    checked = 0
    for _ in range(20000):
        proxy = tuple(10 ** rng.uniform(-4.0, 4.0) for _ in range(3))
        neural = tuple(10 ** rng.uniform(-4.0, 4.0) for _ in range(3))
        field = build(proxy, neural)
        assert math.isfinite(field[0]) and -8.0 <= field[0] <= 8.0
        if field[3] != 3:
            continue
        source = tuple(0.0 if rng.random() < 0.05 else
                       10 ** rng.uniform(-5.0, 4.0) for _ in range(3))
        sy = dot(source)
        if sy <= 1.0 / 65536.0:
            continue
        base = tuple(x / sy for x in source)
        delta = rgb_delta(field[1:3])
        assert math.isclose(dot(delta), 0.0, abs_tol=2e-14)
        ratio = rng.uniform(1.0, 16.0)
        scale = min(rng.uniform(0.0, 2.0), ratio_limit(base, delta, ratio))
        result = tuple(b + d * scale for b, d in zip(base, delta))
        assert all(math.isfinite(x) and x >= -2e-12 for x in result)
        assert math.isclose(dot(result), 1.0, rel_tol=2e-12, abs_tol=2e-12)
        for before, after in zip(base, result):
            assert after <= before * ratio + 2e-12
            assert after + 2e-12 >= before / ratio
        checked += 1
    assert checked > 19000
    print(f'PASS #86 normalized-opponent field: {checked} finite HDR/extreme cases; near-black/invalid fail closed')


if __name__ == '__main__':
    wiring()
    numeric()
