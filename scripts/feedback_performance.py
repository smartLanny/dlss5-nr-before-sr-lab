"""Bounded interpretation of selected current F8 metadata, never log excerpts.

These are rolling, partial GPU-stage observations, not interval costs, game FPS,
or a benchmark. Missing observations remain unavailable, including real zero
values without their explicit timing availability and configuration receipts.
"""
from __future__ import annotations
import math
import re

FIELDS = (
    'requested.config', 'end.dual.currentConfig', 'end.dual.timingConfig',
    'end.dual.timingSamples', 'end.dual.gpuMs.Chain',
    'end.privateSR.gpuTimingAvailable', 'end.privateSR.timingConfig',
    'end.privateSR.timingInstance', 'end.privateSR.gpuSamples',
    'end.privateSR.gpuMedianMs', 'end.privateSR.gpuP95Ms',
)
UINT = re.compile(r'(?:0|[1-9][0-9]{0,19})\Z')
NUMBER = r'(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]{1,3})?'
TRIPLE = re.compile(r'last:(' + NUMBER + r'),median:(' + NUMBER + r'),p95:(' + NUMBER + r')\Z')


def unsigned(value):
    if not isinstance(value, str) or not UINT.fullmatch(value):
        return None
    parsed = int(value)
    return parsed if parsed <= 2**64-1 else None


def milliseconds(value):
    if not isinstance(value, str) or len(value) > 32 or not re.fullmatch(NUMBER, value):
        return None
    parsed = float(value)
    return parsed if math.isfinite(parsed) and 0 <= parsed <= 10000 else None


def unavailable(reason):
    return {'status': 'unavailable', 'reason': reason}


def summarize(info):
    """Return typed values only; never echo submitted text in rendered prose."""
    result = {'whole_frame': 'unavailable', 'scope': 'partial-rolling-gpu-stages'}
    requested = unsigned(info.get('requested.config'))
    current = unsigned(info.get('end.dual.currentConfig'))
    common = None
    if info.get('snapshot.valid') != '1':
        common = 'snapshot-unavailable'
    elif requested is None or current is None or not requested or not current:
        common = 'configuration-unavailable'
    elif requested != current:
        common = 'configuration-changed'
    for name, prefix, sample_key in (
        ('dual_chain', 'end.dual.', 'timingSamples'),
        ('private_sr', 'end.privateSR.', 'gpuSamples'),
    ):
        if common:
            result[name] = unavailable(common)
            continue
        config = unsigned(info.get(prefix+'timingConfig'))
        count = unsigned(info.get(prefix+sample_key))
        if config != current:
            result[name] = unavailable('different-or-missing-configuration')
            continue
        if count is None or not count:
            result[name] = unavailable('no-valid-samples')
            continue
        if name == 'private_sr':
            owner = unsigned(info.get(prefix+'timingInstance'))
            if info.get(prefix+'gpuTimingAvailable') != '1' or owner is None or not owner:
                result[name] = unavailable('availability-or-owner-unproven')
                continue
            median = milliseconds(info.get(prefix+'gpuMedianMs'))
            p95 = milliseconds(info.get(prefix+'gpuP95Ms'))
        else:
            raw = info.get(prefix+'gpuMs.Chain')
            match = TRIPLE.fullmatch(raw) if isinstance(raw, str) and len(raw) <= 110 else None
            values = [milliseconds(v) for v in match.groups()] if match else []
            if len(values) != 3 or any(v is None for v in values):
                result[name] = unavailable('invalid-timing')
                continue
            _, median, p95 = values
        if median is None or p95 is None or p95 < median:
            result[name] = unavailable('invalid-timing')
            continue
        result[name] = {'status': 'observed', 'samples': count,
                        'median_ms': median, 'p95_ms': p95}
    return result


REASONS = {
    'snapshot-unavailable': '配置快照无效',
    'configuration-unavailable': '缺少配置身份',
    'configuration-changed': '采集期间配置已变化',
    'different-or-missing-configuration': '计时不属于已确认的采集配置，或缺少身份',
    'no-valid-samples': '没有有效稳定样本',
    'availability-or-owner-unproven': '计时可用性或所属实例未确认',
    'invalid-timing': '计时字段缺失、无效或不自洽',
}


def render(info):
    result = summarize(info)
    rows = ['## 性能记录：仅部分 GPU 阶段',
            '整帧 GPU 耗时 / 游戏 FPS：未取得。下面的滚动统计不是 F8 采集区间差值，也不能相加或取倒数换算 FPS。']
    for name, title, scope in (
        ('dual_chain', '双层 NR/SR 链路', '仅该链路的 GPU 时间戳跨度，不包含其他游戏工作'),
        ('private_sr', '私有 SR', '仅私有编码、Feature1 重建和合成，不包含原生 SR/RR、NR 或整帧'),
    ):
        item = result[name]
        if item['status'] != 'observed':
            rows.append(f'{title}：未取得（{REASONS[item["reason"]]}）；不是 0 ms。')
        else:
            rows.append(f'{title}：中位 {item["median_ms"]:.6g} ms，P95 {item["p95_ms"]:.6g} ms，'
                        f'{item["samples"]} 个稳定样本；{scope}。')
    rows += ['配置匹配不证明采样时刻、队列或场景相同；这些汇总不能单独证明性能提升。',
             '复测请固定场景、分辨率、NR 层数、模型、HDR 与 FG；先关闭菜单和图像采集，区分首次开启与预热后的帧时间。',
             '持续旁路或没有稳定样本时，请提交原始 F8 与复现步骤；不要关闭保护。旁路后的高 FPS 不是优化收益。']
    return rows
