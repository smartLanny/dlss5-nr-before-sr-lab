#!/usr/bin/env python3
from __future__ import annotations

import math


def powered_ratio(proxy: float, neural: float, strength: float,
                  confidence: float, max_ratio: float) -> float:
    proxy = max(proxy, 1.0 / 4096.0)
    neural = max(neural, 1.0 / 4096.0)
    log_ratio = max(-8.0, min(8.0, math.log2(neural / proxy)))
    limit = math.log2(max(max_ratio, 1.0))
    powered = max(-limit, min(limit, log_ratio * strength * confidence))
    return 2.0 ** powered


def target_luma(source: float, full_proxy: float, ratio: float,
                max_ratio: float) -> float:
    full_proxy = max(full_proxy, 1.0 / 4096.0)
    model_target = full_proxy * ratio
    target = (model_target + (source - full_proxy)
              if source > full_proxy
              else model_target * source / full_proxy)
    if source > 1.0 / 65536.0:
        target = max(source / max_ratio, min(source * max_ratio, target))
    else:
        target = source
    return target


# Strength zero and a no-op model must be exact identities.
for source in (0.001, 0.05, 0.5, 1.0, 4.0, 20.0):
    assert math.isclose(powered_ratio(0.5, 1.0, 0.0, 1.0, 16.0), 1.0)
    assert math.isclose(powered_ratio(0.5, 0.5, 8.0, 1.0, 16.0), 1.0)
    assert math.isclose(target_luma(source, min(source, 0.8), 1.0, 16.0), source)

# Integer strength is mathematically repeated multiplicative application until
# the explicit maximum-ratio guard becomes authoritative.
base = 1.18
for strength in (1.0, 2.0, 4.0, 8.0):
    actual = powered_ratio(1.0, base, strength, 1.0, 16.0)
    expected = min(16.0, base ** strength)
    assert math.isclose(actual, expected, rel_tol=1e-12, abs_tol=1e-12)

# Positive and negative model changes remain monotonic as strength increases.
positive = [powered_ratio(1.0, 1.1, s, 1.0, 16.0)
            for s in (0, 1, 2, 4, 8)]
negative = [powered_ratio(1.0, 0.9, s, 1.0, 16.0)
            for s in (0, 1, 2, 4, 8)]
assert positive == sorted(positive)
assert negative == sorted(negative, reverse=True)

# Confidence continuously fades the exponent rather than blending two already
# clipped outputs.
conf = [powered_ratio(1.0, 1.5, 4.0, c, 16.0)
        for c in (0.0, 0.25, 0.5, 0.75, 1.0)]
assert conf == sorted(conf)
assert math.isclose(conf[0], 1.0)

# HDR headroom above the model proxy is additive: a neutral model leaves it
# untouched, while the model only edits the bounded proxy component.
source = 6.0
proxy = 0.9
for ratio in (0.5, 1.0, 2.0, 4.0):
    target = target_luma(source, proxy, ratio, 16.0)
    expected = proxy * ratio + (source - proxy)
    assert math.isclose(target, expected, rel_tol=1e-12, abs_tol=1e-12)

# The two composition branches meet continuously at source == full proxy.
for ratio in (0.25, 0.75, 1.0, 2.0, 8.0):
    p = 0.8
    left = target_luma(p * (1.0 - 1e-9), p, ratio, 16.0)
    right = target_luma(p * (1.0 + 1e-9), p, ratio, 16.0)
    assert abs(left - right) < 1e-7

# The hard ratio guard contains all extreme model/strength combinations.
for source in (0.001, 0.1, 1.0, 10.0):
    for neural in (1e-9, 1e-4, 1.0, 1e4):
        ratio = powered_ratio(0.5, neural, 8.0, 1.0, 4.0)
        target = target_luma(source, min(source, 0.8), ratio, 4.0)
        assert math.isfinite(target)
        assert source / 4.0 - 1e-12 <= target <= source * 4.0 + 1e-12

print("beta0.4-r2 multiplicative/HDR math validation passed")
