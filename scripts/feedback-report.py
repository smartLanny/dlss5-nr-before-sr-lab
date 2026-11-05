#!/usr/bin/env python3
"""Prepare a local Markdown issue report from an original F8 TXT or F7 ZIPs.

Validates using the production core/image parsers before reading any evidence.
Does not extract files, open URLs, upload, or modify a game directory. A matching
manifest/hash proves consistency, not authenticity or successful game rendering.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
from pathlib import Path
import re
import sys
import zipfile
import zlib

ROOT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('report_core_parser', ROOT / 'parse-core-feedback.py')
core_parser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(core_parser)
spec = importlib.util.spec_from_file_location('report_text_parser', ROOT / 'parse-core-text.py')
text_parser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(text_parser)
spec = importlib.util.spec_from_file_location('report_performance', ROOT / 'feedback_performance.py')
performance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(performance)

FIELDS = (
    ('采集编号', 'session'), ('构建标识', 'build'), ('源码提交', 'artifact.sourceCommit'),
    ('源码有未提交修改', 'artifact.sourceDirty'), ('Addon SHA256', 'artifact.addonSha256'),
    ('身份测量状态', 'artifact.identityStatus'), ('游戏进程名', 'exe.leaf'), ('GPU', 'gpu'),
    ('驱动原始编号', 'driver.raw'), ('API 编号', 'api'), ('显示尺寸', 'display.extent'),
    ('配置快照有效', 'snapshot.valid'), ('配置代数', 'requested.config'), ('请求模式（1后置/2前置）', 'requested.mode'),
    ('插件开关', 'requested.enabled'), ('请求效果强度', 'requested.blend'),
    ('开始旁路原因', 'start.bypass'), ('结束旁路原因', 'end.bypass'),
    ('命令状态现场可用', 'end.commandState.available'),
    ('最近载体判断距采集毫秒', 'end.commandState.ageMs'),
    ('最近载体具体原因', 'end.commandState.reason'), ('最近准入路线', 'end.commandState.route'),
    ('首次 Hook 失败可用（历史）', 'end.ngxHookFailure.available'),
    ('首次 Hook 失败操作（历史）', 'end.ngxHookFailure.operation'),
    ('首次 Hook 错误码（历史）', 'end.ngxHookFailure.status'),
    ('首次 Hook 入口形态（历史）', 'end.ngxHookFailure.entry'),
)


def safe(value) -> str:
    if value is None:
        return '未取得'
    text = str(value)
    if len(text) > 160 or any(c in text for c in ':\\/\r\n'):
        return '[隐藏路径或异常字段]'
    # Do not embed active Markdown, links, terminal escapes or arbitrary Unicode
    # from a submitted archive in a report that will be pasted into an issue.
    return re.sub(r'[^A-Za-z0-9 _.,=()+\-]', '?', text) or '未取得'


def digest(path: Path, limit: int) -> str:
    h = hashlib.sha256()
    total = 0
    with path.open('rb') as stream:
        while block := stream.read(64 * 1024):
            total += len(block)
            core_parser.require(total <= limit, 'archive grew beyond budget during report generation')
            h.update(block)
    return h.hexdigest()


def delta(info, key):
    before, after = info.get('start.' + key), info.get('end.' + key)
    if not all(isinstance(v, str) and re.fullmatch(r'[0-9]{1,20}', v) for v in (before, after)):
        return '未取得'
    if int(after) < int(before):
        return '计数重置，不能计算'
    return str(int(after) - int(before))


def prepare(core: Path, image: Path | None = None, symptom: str = 'compatibility') -> str:
    # Inspect original completed files, including explicitly partial captures.
    # A TXT never enters the ZIP pairing path or inherits manifest verification.
    with core.open('rb') as source:
        is_text = source.read(5) == b'DLSS5'
    if is_text:
        core_parser.require(image is None, 'F8 TXT cannot verify an F7 image pair; use the new F7 core ZIP')
        fields = [key for _, key in FIELDS] + ['start.nrSuccess', 'end.nrSuccess',
                  'start.transferSubmitted', 'end.transferSubmitted']
        if symptom == 'performance':
            fields += list(performance.FIELDS)
        result = text_parser.inspect_text(core, fields)
    else:
        result = core_parser.inspect_pair(core, image)['core']
    info = result['metadata']
    title = {'compatibility': '不生效 / 旁路 / 兼容性', 'visual': '颜色 / 闪烁 / 画面异常', 'crash': '崩溃 / 卡住', 'performance': '卡顿 / 低帧率 / 首次开启停顿'}[symptom]
    rows = ['# DLSS5 问题报告', '', '## 请补充', f'问题类型：{title}',
            '游戏名称与版本：', '问题现象：', '复现步骤：', '其他插件与 HDR / FG 状态：', '',
            '## 已检查的 F8 文本记录' if is_text else '## 已验证的包内记录',
            '以下是采集时的记录，不是当前游戏状态，也不是官方签名或兼容性认证。',
            f'核心必需证据：{"完整" if result["required_complete"] else "部分完成（仍可分析）"}',
            f'全部采集项：{"完整" if result["complete"] else "部分完成"}',
            f'F8 TXT SHA256：{result["sha256"]}' if is_text else f'核心 ZIP SHA256：{digest(core, core_parser.LIMIT)}']
    if is_text:
        rows.append('已检查原始 TXT 的结构与所含摘要哈希；TXT 没有 ZIP manifest，也不能用于验证 F7 图像配对。')
    rows += [f'{label}：{safe(info.get(key))}' for label, key in FIELDS]
    rows += [f'观测区间 NR 成功调用增量：{delta(info, "nrSuccess")}',
             f'观测区间合成提交增量：{delta(info, "transferSubmitted")}',
             '调用/提交计数不等于 GPU 完成或画面效果已验证。']
    if image is not None:
        rows += ['核心/图像记录配对：通过（不是同帧或完整图像序列证明）',
                 f'图像 ZIP SHA256：{digest(image, 512 * 1024 * 1024)}']
    else:
        rows.append('图像包：未附。普通不生效反馈只需 F8 TXT；画面异常才补 F7。')
    rows += ['', '## 需要关注的证据']
    if not result['required_complete']:
        rows.append('F8 文本有必需证据缺失或采集未完成；保留这个文件，不要直接丢弃。' if is_text else
                    '核心包有必需证据缺失或采集未完成；保留这个包，不要直接丢弃。')
    if result['missing']:
        location = 'TXT 的 evidence 记录' if is_text else '包内 manifest'
        rows.append(f'记录了 {len(result["missing"])} 项缺失；具体原因见{location}，不粘贴原始私人日志。')
    if is_text:
        evidence = '\n'.join(result['summaries'].values()).lower()
    else:
        evidence = ''
        with zipfile.ZipFile(core) as archive:
            for entry in archive.infolist():
                if core_parser.ENTRY.fullmatch(entry.filename):
                    evidence += archive.read(entry).decode('ascii').lower() + '\n'
    # Historical clues only. Logs can include recoveries or earlier sessions;
    # never override current counters/bypass solely on a keyword match.
    if 'another reshade instance' in evidence and 'aborting initialization' in evidence:
        rows.append('历史摘要出现 ReShade 重复实例并中止初始化。先备份并核对加载路径；不要只凭 d3d12.dll/dxgi.dll 文件名盲删，也不保证这是唯一故障。')
    if 'null-pso-policy-disabled' in evidence or 'null-guard-' in evidence:
        rows.append('历史摘要出现空 PSO 安全旁路。这不是强度不足；保留安全保护，需进一步验证命令状态，不能直接宣称已修复。')
    if any(value in evidence for value in ('device removed', 'device hung', 'device_removed', 'device_hung')):
        rows.append('历史摘要出现设备移除/挂起线索；需同一构建的离线崩溃证据，不能仅凭关键字归因于驱动。')
    if info.get('end.ngxHookFailure.entry') == 'existing-short-hotpatch':
        rows.append('首次 Hook 失败入口是已有短热补丁。不要覆盖第三方前缀或匿名转发器；需单独验证 FG/MFG 共存。该记录不能证明具体插件是唯一根因。')
    if info.get('end.commandState.route') == 'pristine-null':
        rows.append('最近一次载体通过了窄范围空基线准入；这不等于所有帧持续生效或游戏已验收。完整状态见 core 报告的 start/end.commandState 字段。')
    if symptom == 'crash':
        rows.append('游戏已退出时使用离线崩溃收集器。F8 不是 dump/异常栈的替代品，不要公开上传未经检查的 dump。')
    if symptom == 'visual':
        rows.append('F7 会生成新核心包与图像包；请提交新的一对，不能用旧 F8 包与新 F7 图像混配。')
    if symptom == 'performance':
        rows += ['', *performance.render(info)]
    rows += ['', '## 附件',
             '请手动附上本报告对应的原始 F8 TXT；不需要另做 ZIP。' if is_text else
             '请手动附上本报告对应的核心 ZIP；有配对图像时同时附上图像 ZIP。',
             '报告不含本机绝对路径，不上传任何文件；提交前请核对内容并补充问题描述。', '']
    return '\n\n'.join(rows)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('core', type=Path, help='Original F8 TXT or completed F7 core ZIP (partial evidence is supported)')
    p.add_argument('--image', type=Path, help='F7 image ZIP; requires its new core ZIP, never an older F8 TXT')
    p.add_argument('--symptom', choices=('compatibility', 'visual', 'crash', 'performance'), default='compatibility')
    p.add_argument('--output', type=Path, help='Create a new UTF-8 Markdown file; never overwrite an existing file')
    args = p.parse_args(argv)
    try:
        report = prepare(args.core, args.image, args.symptom)
        if args.output:
            with args.output.open('x', encoding='utf-8', newline='\n') as output:
                output.write(report)
        else:
            print(report)
    except (OSError, ValueError, KeyError, TypeError, RecursionError, zipfile.BadZipFile, zlib.error) as error:
        # The detailed exception may contain a private input path or metadata.
        print(f'Cannot prepare feedback report ({type(error).__name__}); use the original F8 TXT or matching F7 ZIPs and check the new output path locally.', file=sys.stderr)
        return 2
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
