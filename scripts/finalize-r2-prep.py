"""One-shot preparation, removed from final PR tree. Never moves Git refs."""
from pathlib import Path
import hashlib, json, os, subprocess, urllib.request

assert os.environ['GITHUB_REPOSITORY'] == 'smartLanny/dlss5-nr-before-sr-lab'
assert os.environ['GITHUB_REF'] == 'refs/heads/codex/043r2-finalize-preparation'
BASE='1e2d2c1d2d80126d04f021bde0c36138dcee3f21'
changed={}
def read(path):
    if path not in changed:
        data=Path(path).read_bytes()
        old=subprocess.check_output(['git','show',BASE+':'+path])
        assert data==old, 'Input changed since reviewed base: '+path
        changed[path]=data.decode('utf-8-sig')
    return changed[path]
def edit(path,old,new):
    text=read(path)
    assert text.count(old)==1,(path,old[:120],text.count(old))
    changed[path]=text.replace(old,new)

# Ordinary version strings only; archived release notes and format/schema IDs
# are intentionally not rewritten. No author/channel/runtime ownership changes.
versioned=['src/nr_before_sr.cpp','src/nr_before_sr.rc','scripts/build.ps1',
    'scripts/package-beta03.ps1','scripts/collect-feedback.ps1','scripts/collect-crash-feedback.ps1',
    'config/nr_before_sr.ini','README.md','docs/UPDATE.en.txt','docs/UPDATE.zh-CN.txt',
    'docs/INSTALL.en.txt','docs/INSTALL.zh-CN.txt','docs/INSTALL.en.md']
for path in versioned:
    text=read(path)
    assert 'beta0.4.3' in text,path
    changed[path]=text.replace('beta0.4.3','beta0.4.3r2')
rc='src/nr_before_sr.rc'
changed[rc]=changed[rc].replace('0,4,3,0','0,4,3,2').replace('0.4.3.0 beta','0.4.3.2 beta r2')
changed[rc]=changed[rc].replace('FILEFLAGS 0x0L','FILEFLAGS VS_FF_PRERELEASE')
cpp='src/nr_before_sr.cpp'
edit(cpp,'#include "nr_feedback_ticket.h"','#include "nr_feedback_ticket.h"\n#include "nr_command_evidence.h"')

# Keep every reason's latest state compact, so worst-case identifiers/counts
# cannot overrun the bounded core metadata allocation.
h='src/nr_command_evidence.h'
changed[h]=Path(h).read_text()
edit(h,'        w.EventLine("lastForReason", unsigned(i), s.first[i].latest, s.first[i].count);',
'''        const auto &e = s.first[i].latest;
        w.Put("commandEvidence.lastForReason.%u=tick:%llu reset:%llu pipelineWrites:%llu dirty:%u bindings:%u\\n",
              unsigned(i), static_cast<unsigned long long>(e.tick),
              static_cast<unsigned long long>(e.reset), static_cast<unsigned long long>(e.pipeline_writes),
              e.dirty, e.bindings);''')

state='src/nr_command_state.inl'
edit(state,'// BEGIN COMMAND_STATE_MODEL (extracted unchanged by the regression harness)',
'#include "nr_command_evidence.h"\n\n// BEGIN COMMAND_STATE_MODEL (extracted unchanged by the regression harness)')
edit(state,'std::atomic<unsigned long long> g_command_state_bypasses{0};',
'''std::atomic<unsigned long long> g_command_state_bypasses{0};
// Dedicated leaf lock: no GPU calls, formatting, logging or allocations under
// this lock. Readers never acquire the chain/state/hook locks while holding it.
SRWLOCK g_command_evidence_lock = SRWLOCK_INIT;
nr_command_evidence::State g_command_evidence{};
void ObserveCommandEvidence(const nr_command_evidence::Event &event) {
    AcquireSRWLockExclusive(&g_command_evidence_lock);
    nr_command_evidence::Observe(g_command_evidence, event);
    ReleaseSRWLockExclusive(&g_command_evidence_lock);
}
void ObserveCommandHookFailure(const char *stage, int status, unsigned request, unsigned batch) {
    const auto tick = GetTickCount64();
    AcquireSRWLockExclusive(&g_command_evidence_lock);
    nr_command_evidence::ObserveHookFailure(g_command_evidence, stage, status, request, batch, tick);
    ReleaseSRWLockExclusive(&g_command_evidence_lock);
}
bool DescribeCommandEvidence(char *out, std::size_t capacity) {
    if (out && capacity) out[0] = 0;
    auto *snapshot = new (std::nothrow) nr_command_evidence::State{};
    if (!snapshot) return false;
    if (!TryAcquireSRWLockShared(&g_command_evidence_lock)) { delete snapshot; return false; }
    *snapshot = g_command_evidence;
    ReleaseSRWLockShared(&g_command_evidence_lock);
    const bool complete = nr_command_evidence::Format(*snapshot, out, capacity);
    delete snapshot;
    return complete;
}''')
edit(state,'        static std::atomic<unsigned int> rejected_type{0};',
'''        nr_command_evidence::Event evidence{};
        nr_command_evidence::Identifier(evidence.reason, sizeof(evidence.reason), "direct-list-required");
        evidence.tick = GetTickCount64(); ObserveCommandEvidence(evidence);
        static std::atomic<unsigned int> rejected_type{0};''')
edit(state,'    bool null_baseline=false;\n    AcquireSRWLockExclusive(&g_command_state_lock);',
'''    bool null_baseline=false;
    nr_command_evidence::Event evidence{};
    evidence.token = token; evidence.tick = GetTickCount64();
    evidence.expanded_tracking_requested = AllowExperimentalNullPsoCarrier();
    AcquireSRWLockExclusive(&g_command_state_lock);''')
edit(state,'        owner = record->owner; native = record->native;',
'''        owner = record->owner; native = record->native;
        evidence.registered = true; evidence.hooks = record->hooks_ready;
        evidence.meta_known = record->meta_contract.known;
        evidence.meta_init = record->meta_contract.initialization_dirty;
        evidence.meta_execute = record->meta_contract.execution_dirty;''')
edit(state,'        captured = meta_covered && record->state != nullptr && record->hooks_ready &&',
'''        evidence.meta_covered = meta_covered;
        captured = meta_covered && record->state != nullptr && record->hooks_ready &&''')
edit(state,'            baseline_origin=record->state->baseline_origin; null_baseline=record->state->null_pipeline_baseline;',
'''            baseline_origin=record->state->baseline_origin; null_baseline=record->state->null_pipeline_baseline;
            const auto &observed = *record->state;
            evidence.known = observed.known; evidence.dirty = observed.unknown_domains;
            evidence.reset = observed.reset_serial; evidence.pipeline_writes = observed.pipeline_writes;
            evidence.null_baseline = observed.null_pipeline_baseline; evidence.dynamic = observed.dynamic.recorded;
            evidence.baseline = std::strcmp(observed.baseline_origin, "Reset") == 0 ? 1u :
                std::strcmp(observed.baseline_origin, "ClearState") == 0 ? 2u : 0u;
            evidence.bindings = (observed.pipeline ? 1u : 0u) | (observed.raytracing_pipeline ? 2u : 0u) |
                (observed.roots[0] ? 4u : 0u) | (observed.roots[1] ? 8u : 0u) |
                (observed.heaps[0] ? 16u : 0u) | (observed.heaps[1] ? 32u : 0u);''')
edit(state,'    ReleaseSRWLockExclusive(&g_command_state_lock);\n    if (needs_install) {',
'''    evidence.admitted = captured;
    nr_command_evidence::Identifier(evidence.reason, sizeof(evidence.reason), captured ? "admitted" : unavailable_reason);
    ReleaseSRWLockExclusive(&g_command_state_lock);
    ObserveCommandEvidence(evidence); // Only a scalar snapshot; no native pointers escape.
    if (needs_install) {''')
edit(state,'    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) ok = false;',
'''    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        ok = false; ObserveCommandHookFailure("initialize", initialized, 0, count);
    }''')
edit(state,'    if (count > 64) ok = false;',
'''    if (count > 64) { ok = false; ObserveCommandHookFailure("request-capacity", -1, count, count); }''')
edit(state,'            if (i == 4 || request.target == nullptr || !PinHookAddress(request.target, "D3D12 command state", nullptr)) { ok = false; break; }\n            if (MH_CreateHook(request.target, request.detours[i], &family.originals[i]) != MH_OK) { ok = false; break; }',
'''            if (i == 4 || request.target == nullptr) {
                ok = false; ObserveCommandHookFailure("target-capacity-or-null", -1, r, count); break;
            }
            if (!PinHookAddress(request.target, "D3D12 command state", nullptr)) {
                ok = false; ObserveCommandHookFailure("pin-win32", static_cast<int>(GetLastError()), r, count); break;
            }
            const auto created = MH_CreateHook(request.target, request.detours[i], &family.originals[i]);
            if (created != MH_OK) { ok = false; ObserveCommandHookFailure("create-minhook", created, r, count); break; }''')
edit(state,'        if (status != MH_OK && status != MH_ERROR_ENABLED) { ok = false; break; }',
'''        if (status != MH_OK && status != MH_ERROR_ENABLED) {
            ok = false; ObserveCommandHookFailure("queue-enable-minhook", status, r, count); break;
        }''')
edit(state,'    if (ok) ok = MH_ApplyQueued() == MH_OK;',
'''    if (ok) {
        const auto applied = MH_ApplyQueued(); ok = applied == MH_OK;
        if (!ok) ObserveCommandHookFailure("apply-minhook", applied, count, count);
    }''')
# No relaxed flags, extra hooks, clears or altered MinHook rollback semantics.
core='src/nr_feedback_core.inl'
edit(core,'    bool snapshot_ok = false;',
'''    bool snapshot_ok = false;
    bool command_evidence_ok = false;
    char command_evidence[nr_command_evidence::kTextCapacity] = {}; // CoreSession is worker-owned heap storage.''')
edit(core,'#include "nr_feedback_ticket.h"','#include "nr_feedback_ticket.h"\n#include "nr_command_evidence.h"')
edit(core,'        FeedbackAppendActivity(s, "end");',
'''        FeedbackAppendActivity(s, "end");
        session->command_evidence_ok = DescribeCommandEvidence(session->command_evidence, sizeof(session->command_evidence));
        if (session->command_evidence_ok) FeedbackAppend(s, "%s", session->command_evidence);
        else FeedbackAppend(s, "commandEvidence.available=0\\ncommandEvidence.reason=snapshot-busy-allocation-or-capacity\\n");''')
edit(core,'            CollectLogs(*job, *session);',
'''            CollectLogs(*job, *session);
            if (!session->command_evidence_ok)
                nr_bundle::AddMissing(*job, "runtime/command-evidence", "bounded command evidence snapshot unavailable", true);''')
# Windows collection test runs the real format with a separately declared
# producer seam; native harness below exercises the actual producer.
coretest='scripts/validate-core-feedback.py'
edit(coretest,'#include "nr_diagnostic_bundle.inl"',
'''#include "nr_diagnostic_bundle.inl"
#include "nr_command_evidence.h"
bool evidence_busy=false;
bool DescribeCommandEvidence(char *out,std::size_t capacity){
    if(evidence_busy){if(capacity)out[0]=0;return false;}
    nr_command_evidence::State empty{};
    return nr_command_evidence::Format(empty,out,capacity);
}''')
edit(coretest,'    save("valid-core.zip");',
'''    check(std::strstr(session_unused_placeholder, "never") == nullptr, "placeholder");
    save("valid-core.zip");''')
# Verify emitted metadata by opening its actual packaged source, not a fake
# asserted live NR counter. The original session has already been destroyed.
edit(coretest,'    check(std::strstr(session_unused_placeholder, "never") == nullptr, "placeholder");\n','')
edit(coretest,'    snapshot_busy=true;begin(runtime);drain();snapshot_busy=false;',
'''    evidence_busy=true;begin(runtime);drain();evidence_busy=false;
    check(g_feedback_status==4 && !nr_core_feedback::SnapshotPair().complete,"unavailable command evidence is explicitly partial");
    snapshot_busy=true;begin(runtime);drain();snapshot_busy=false;''')
native='tests/command_state_native_harness.cpp.in'
edit(native,'    Check(CaptureNativeCommandState(list, *snapshot), "unknown signature recovers on reset");',
'''    Check(CaptureNativeCommandState(list, *snapshot), "unknown signature recovers on reset");
    auto *evidence_text = new char[nr_command_evidence::kTextCapacity];
    Check(DescribeCommandEvidence(evidence_text,nr_command_evidence::kTextCapacity), "actual command evidence snapshot formatted");
    Check(std::strstr(evidence_text,"reason:unknown-indirect-signature") != nullptr,
        "first real unknown signature remains after recovery");
    Check(std::strstr(evidence_text,"reason:admitted") != nullptr,
        "actual gate recovery retained, not asserted as GPU success");
    delete[] evidence_text;''')
ns='scripts/validate-command-state-native.ps1'
edit(ns,'"-I$(Join-Path $minHook \'include\')" $generated',
'"-I$(Join-Path $minHook \'include\')" "-I$(Join-Path $projectRoot \'src\')" $generated')

pack='scripts/package-beta03.ps1'
edit(pack,"$CrashFeedbackLauncher = Join-Path $ProjectRoot 'scripts\\Collect-Crash-Feedback.cmd'",
'''$CrashFeedbackLauncher = Join-Path $ProjectRoot 'scripts\\Collect-Crash-Feedback.cmd'
$ReportScripts = @('feedback-report.py', 'parse-core-feedback.py', 'parse-f8-bundle.py') |
    ForEach-Object { Join-Path $ProjectRoot ('scripts\\' + $_) }''')
edit(pack,') + $Packages.Readme',') + $Packages.Readme + $ReportScripts')
edit(pack,'    Copy-Item -LiteralPath $CrashFeedbackLauncher -Destination $PackagePath',
'''    Copy-Item -LiteralPath $CrashFeedbackLauncher -Destination $PackagePath
    Copy-Item -LiteralPath $ReportScripts -Destination $PackagePath''')
# Full install payload now carries 3 adjacent optional analysis scripts.
for p in ['docs/INSTALL.en.txt','docs/INSTALL.zh-CN.txt','docs/INSTALL.en.md']:
    changed[p]=read(p).replace('nine files','twelve files').replace('**nine files**','**twelve files**').replace('9 个','12 个').replace('9个','12个').replace('九个','十二个')
for p in ['docs/UPDATE.en.txt','docs/INSTALL.en.txt','docs/INSTALL.en.md']:
    changed[p]+='\n\nR2 feedback: use the Feedback tab and Copy report summary after F8; attach the same core ZIP. Visual problems require F7 and its NEW core/image pair. Optional analysis scripts need Python 3.11+, but in-game collection/copying does not. Core metadata now includes bounded process-lifetime command-gate evidence, not GPU completion or complete tracing. Offline CMD packages remain a separate rescue format, not the runtime 8 MiB core schema.\n'
for p in ['docs/UPDATE.zh-CN.txt','docs/INSTALL.zh-CN.txt']:
    changed[p]+='\n\nr2 反馈：F8 完成后到“反馈与报告”复制报告摘要，补充现象并附同次核心 ZIP。画面问题再采 F7，提交它新生成的一对包。附带的 Python 分析脚本供分析者使用，需要 Python 3.11+；玩家在游戏内采集和复制摘要不需要安装 Python。核心包增加有界命令状态证据，不能据此声称 GPU 已完成或游戏兼容。离线 CMD 是独立救援格式，不冒充 F8 的 8 MiB 核心格式。\n'
notes='''# beta0.4.3r2 待合并版本\n\n本次收尾固化 r2 版本名与 Windows 0.4.3.2 文件属性；不创建 Release、不合并 PR、不修改 main，也不冒充已签名发行包。\n\n## 实际更新\n- 保留 PR #61 的后置原生 SR 输出快照、重置帧保护、三个 UI 页签和复制反馈摘要。\n- F8 不再只依赖日志文件裁剪：从内存读取前 16 类命令准入原因的首次/最近记录、计数、最近 8 次原因转换，以及具体 Hook 发布失败阶段/错误码。记录 Reset 来源与序号、PSO 写入次数、根签名/堆存在位、meta dirty 和覆盖状态。\n- 明确“没有观察到”与“根本没发生”不同：默认未开启扩展图形 setter 观察时，dynamic=0 不是状态为空的证据。列表编号不是原始指针；不复制描述符地址或游戏资源内容。\n- 固定大小内存、无逐帧文件日志；F8 工作线程取快照并格式化。取不到证据或空间不足时标为 partial，不复用上次记录。统计为进程生命周期内的准入观测，不是假装全部帧追踪或 GPU 执行证明。\n- 中英文包带离线收集/崩溃收集工具及三份相邻 Python 报告脚本。无需玩家为 F8 另装 Python。\n\n## 尚未修好\n沉没之城2 #38、地铁 #50、桥接 #58 等 null-PSO 问题仍未修好。本次不关闭守卫、不默认加载额外高风险 Hook，不宣称记录更多信息就一定能在下一轮修复。现有证据已经能识别阻断；下一步是据此实现和验证安全恢复或隔离执行方案。#49 崩溃仍需异常栈/设备移除证据；普通日志不能替代 dump。\n\n## 验证与发布\n当前 PR 的最新 head 必须重新通过完整 Windows/Linux、实际采集 ZIP、命令状态/小栈/WARP、FP16 快照、静态分析及中英文构建。所有编译/CI/WARP 结果与真实 NVIDIA 模型、游戏画面、性能验收分开。合并和正式签名发布由维护者执行。\n'''
changed['docs/BETA_0_4_3R2_RELEASE_NOTES.zh-CN.md']=notes
readme='README.md'
edit(readme,'[community release notes](docs/BETA_0_4_3_RELEASE_NOTES.zh-CN.md)',
'[community release notes](docs/BETA_0_4_3R2_RELEASE_NOTES.zh-CN.md)')
doc='docs/043R2_COMPAT_FEEDBACK.md'
edit(doc,'actual addon SHA256; final r2 release identity is a separate freeze step.',
'actual addon SHA256. Final PR now uses beta0.4.3r2 / Windows 0.4.3.2 identity; signing and release publication remain separate.')
changed[doc]+='\n## Targeted command evidence\n\nF8 now snapshots bounded process-lifetime gate observations directly, independently of log-file retention. First/latest records for up to 16 reasons, an 8-transition ring, admission/rejection/recovery counters, Reset/PSO/meta/coverage fields and first/latest hook failure stage/status are included. No native addresses or GPU data are stored. This is not exhaustive tracing; first-reason overflow is explicit. Expanded graphics setter coverage is NOT available when the existing experiment is off; zero dynamic masks do not establish safe null-state restoration. No extra hooks or relaxed admission are enabled. Snapshot allocation/lock/capacity failures make the core partial. This improves the ability to choose a fix, not a promise of next-iteration compatibility for #38.\n'
ci='.github/workflows/beta042-validate.yml'
read(ci)
changed[ci]=changed[ci].replace('beta0.4.3','beta0.4.3r2').replace('beta043-','beta043r2-')
edit(ci,'          python3 scripts/validate-043r2.py --compiler g++',
'''          python3 scripts/validate-043r2.py --compiler g++
          g++ -std=c++20 -O2 -Wall -Wextra -Werror -Isrc tests/command_evidence_test.cpp -o /tmp/command-evidence
          /tmp/command-evidence''')
edit(ci,'          python scripts/validate-core-feedback.py --compiler $compiler',
'''          & $compiler -std=c++20 -O2 -Wall -Wextra -Werror -static -Isrc tests/command_evidence_test.cpp -o command-evidence.exe
          if ($LASTEXITCODE -ne 0) { throw 'Command evidence policy compilation failed' }
          ./command-evidence.exe
          if ($LASTEXITCODE -ne 0) { throw 'Command evidence policy test failed' }
          python scripts/validate-core-feedback.py --compiler $compiler''')
edit(ci,'          Copy-Item docs/COMMAND_STATE_CONTRACT.md dist/COMMAND-STATE-NOTES.md',
'''          Copy-Item docs/COMMAND_STATE_CONTRACT.md dist/COMMAND-STATE-NOTES.md
          Copy-Item docs/BETA_0_4_3R2_RELEASE_NOTES.zh-CN.md dist/R2-RELEASE-NOTES.md
          Copy-Item docs/043R2_COMPAT_FEEDBACK.md dist/R2-FEEDBACK-NOTES.md
          Copy-Item scripts/Collect-Feedback.cmd,scripts/collect-feedback.ps1,scripts/Collect-Crash-Feedback.cmd,scripts/collect-crash-feedback.ps1 dist/
          Copy-Item scripts/feedback-report.py,scripts/parse-core-feedback.py,scripts/parse-f8-bundle.py dist/
          python dist/feedback-report.py --help
          if ($LASTEXITCODE -ne 0) { throw 'Packaged report dependency smoke test failed' }
          python dist/parse-core-feedback.py --help
          if ($LASTEXITCODE -ne 0) { throw 'Packaged core parser smoke test failed' }
          foreach ($addon in Get-ChildItem dist/*.addon64) {
            if ($addon.VersionInfo.FileVersion -ne '0.4.3.2 beta r2') { throw 'r2 binary resource version mismatch' }
          }
          ./scripts/package-beta03.ps1 -ReleaseRoot "$pwd/r2-update-packages" -UpdateOnly
          if ($LASTEXITCODE -ne 0) { throw 'r2 update package creation failed' }
          python scripts/validate-r2-package.py r2-update-packages
          if ($LASTEXITCODE -ne 0) { throw 'r2 update ZIP content validation failed' }
          Copy-Item r2-update-packages/*.zip dist/''')
# Fail early on tracked drift: candidates must compile the actual committed tree.
edit(ci,'          New-Item -ItemType Directory -Path dist -Force | Out-Null',
'''          if ((git status --porcelain --untracked-files=no)) { throw 'Tracked inputs changed during validation' }
          New-Item -ItemType Directory -Path dist -Force | Out-Null''')

for path,text in changed.items(): Path(path).write_text(text,encoding='utf-8',newline='\n')
subprocess.run(['git','diff','--check'],check=True)
subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-Isrc','tests/command_evidence_test.cpp','-o','/tmp/evidence'],check=True)
subprocess.run(['/tmp/evidence'],check=True)
subprocess.run(['python3','scripts/validate-043r2.py','--compiler','g++'],check=True)
subprocess.run(['python3','scripts/validate-bug-sweep.py','--compiler','g++'],check=True)
subprocess.run(['python3','-m','unittest','discover','-s','tests','-p','test*feedback*.py','-v'],check=True)
# Human-readable exact patch for source review, without dumping base source.
print(subprocess.check_output(['git','diff','--stat'],text=True),flush=True)
print(subprocess.check_output(['git','diff','--','src/nr_command_state.inl','src/nr_feedback_core.inl','scripts/validate-core-feedback.py'],text=True),flush=True)
for path,text in changed.items():
    data=text.encode(); expected=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
    request=urllib.request.Request('https://api.github.com/repos/smartLanny/dlss5-nr-before-sr-lab/git/blobs',
        data=json.dumps({'content':text,'encoding':'utf-8'}).encode(),
        headers={'Authorization':'Bearer '+os.environ['GH_TOKEN'],'Accept':'application/vnd.github+json'},method='POST')
    with urllib.request.urlopen(request,timeout=30) as response: sha=json.load(response)['sha']
    assert sha==expected
    print('FINAL_BLOB '+json.dumps({'path':path,'mode':'100644','type':'blob','sha':sha}),flush=True)
