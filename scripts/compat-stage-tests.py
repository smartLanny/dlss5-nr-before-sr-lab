from pathlib import Path

def read(path):
    return Path(path).read_text(encoding='utf-8-sig')
def write(path, value):
    Path(path).write_text(value, encoding='utf-8', newline='\n')
def replace(path, old, new):
    s=read(path)
    assert s.count(old)==1, (path, old[:100], s.count(old))
    write(path,s.replace(old,new))

replace('tests/command_state_native_harness.cpp.in',
    'bool AllowExperimentalNullPsoCarrier() { return true; }',
    'bool g_allow_experimental = true;\nbool AllowExperimentalNullPsoCarrier() { return g_allow_experimental; }')
p='tests/command_state_native_harness.cpp.in'
s=read(p)
marker='int main(int argc,char **argv) {'
if marker not in s: marker='int main(int argc, char **argv) {'
assert s.count(marker)==1
s=s.replace(marker,'// @PRISTINE_NULL_TEST@\n'+marker+'\n    if (argc>1 && std::strcmp(argv[1],"--pristine-null")==0) return RunPristineNullGate(false);\n    if (argc>1 && std::strcmp(argv[1],"--pristine-null-negative")==0) return RunPristineNullGate(true);')
write(p,s)
replace('scripts/validate-command-state-native.ps1',
    "$harness = $harness.Replace('// @PRODUCTION_COMMAND_STATE@', $production)",
    "$harness = $harness.Replace('// @PRODUCTION_COMMAND_STATE@', $production)\n$pristine = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\\pristine_null_native.inl') -Raw\n$harness = $harness.Replace('// @PRISTINE_NULL_TEST@', $pristine)")
replace('scripts/validate-command-state-native.ps1',"& $binary '--default-enabled'",
    "& $binary '--pristine-null'\nif ($LASTEXITCODE -ne 0) { throw 'Default pristine-null native/GPU validation failed.' }\n& $binary '--pristine-null-negative'\nif ($LASTEXITCODE -ne 42) { throw 'Pristine-null negative control did not detect the deliberately omitted restore.' }\n& $binary '--default-enabled'")
replace('scripts/validate-null-pso-policy.py',
    '"""Verify that Reset(NULL) carriers are fail-closed unless explicitly enabled."""',
    '"""Verify the raw null predicate and full-dynamic replay remain default-off.\n\nThe separately verified pristine subset is exercised by the native/GPU harness\nand validate-compat-sweep.py. This test does not claim all null carriers reject.\n"""')

p='scripts/validate-beta04-structure.py'
s=read(p)
a=s.index('identity_requirements = {');b=s.index('\nfor label, tokens in identity_requirements.items()',a)
block=s[a:b]
for label in ['source','resource','build','package','config','Chinese update','English update']:
    import re
    pattern=r'(    "'+re.escape(label)+r'": \[)([\s\S]*?)(\],)'
    match=re.search(pattern,block)
    assert match, label
    body=match.group(2).replace('beta0.4.3','beta0.4.3r2').replace('0,4,3,0','0,4,3,2').replace('0.4.3.0 beta','0.4.3r2 beta').replace('community candidate','community maintenance release')
    block=block[:match.start(2)]+body+block[match.end(2):]
s=s[:a]+block+s[b:]
s=s.replace('kAddonVersion = "beta0.4.3"','kAddonVersion = "beta0.4.3r2"').replace('kBuildIdentity = "beta0.4.3"','kBuildIdentity = "beta0.4.3r2"')
s=s.replace('beta0.4.3 perfect-chain structural','beta0.4.3r2 perfect-chain structural')
write(p,s)

replace('scripts/feedback-report.py',
    "    ('开始旁路原因', 'start.bypass'), ('结束旁路原因', 'end.bypass'),",
    "    ('开始旁路原因', 'start.bypass'), ('结束旁路原因', 'end.bypass'),\n    ('命令状态现场可用', 'end.commandState.available'),\n    ('最近载体判断距采集毫秒', 'end.commandState.ageMs'),\n    ('最近载体具体原因', 'end.commandState.reason'), ('最近准入路线', 'end.commandState.route'),\n    ('首次 Hook 失败可用（历史）', 'end.ngxHookFailure.available'),\n    ('首次 Hook 失败操作（历史）', 'end.ngxHookFailure.operation'),\n    ('首次 Hook 错误码（历史）', 'end.ngxHookFailure.status'),\n    ('首次 Hook 入口形态（历史）', 'end.ngxHookFailure.entry'),")
replace('scripts/feedback-report.py',
    "    if 'null-pso-policy-disabled' in evidence:",
    "    if 'null-pso-policy-disabled' in evidence or 'null-guard-' in evidence:")
replace('scripts/feedback-report.py',"    if symptom == 'crash':",'''    if info.get('end.ngxHookFailure.entry') == 'existing-short-hotpatch':
        rows.append('首次 Hook 失败入口是已有短热补丁。不要覆盖第三方前缀或匿名转发器；需单独验证 FG/MFG 共存。该记录不能证明具体插件是唯一根因。')
    if info.get('end.commandState.route') == 'pristine-null':
        rows.append('最近一次载体通过了窄范围空基线准入；这不等于所有帧持续生效或游戏已验收。完整状态见 core 报告的 start/end.commandState 字段。')
    if symptom == 'crash':''')
replace('tests/test_feedback_report.py','    def test_counter_reset_and_invalid_values(self):',r'''    def test_hotpatch_evidence_is_not_an_unlock_recommendation(self):
        path = self.f.bundle()
        self.f.rewrite(path, lambda m,i,p: i.update({'end.ngxHookFailure.available':'1',
            'end.ngxHookFailure.entry':'existing-short-hotpatch', 'end.ngxHookFailure.status':'8'}))
        text = report.prepare(path)
        self.assertIn('existing-short-hotpatch', text)
        self.assertIn('不要覆盖', text)

    def test_pristine_route_is_last_sample_not_universal_compatibility(self):
        path = self.f.bundle()
        self.f.rewrite(path, lambda m,i,p: i.update({'end.commandState.available':'1',
            'end.commandState.route':'pristine-null', 'end.commandState.ageMs':'999'}))
        text = report.prepare(path)
        self.assertIn('pristine-null', text)
        self.assertIn('最近一次', text)
        self.assertIn('999', text)

    def test_new_field_path_injection_stays_redacted(self):
        path = self.f.bundle()
        self.f.rewrite(path, lambda m,i,p: i.update({'end.ngxHookFailure.operation':r'C:\Users\SECRET\mod.dll'}))
        self.assertNotIn('SECRET', report.prepare(path))

    def test_counter_reset_and_invalid_values(self):''')

p='scripts/validate-core-feedback.py'
s=read(p)
old=r'''void FeedbackAppendActivity(FeedbackSession &s,const char *prefix){FeedbackAppend(s,"%s.nrSuccess=0\n%s.transferSubmitted=0\n%s.bypass=null-pso-policy-disabled\n",prefix,prefix,prefix);}'''
assert s.count(old)==1
s=s.replace(old,'''#define NR_HAS_COMMAND_STATE_EVIDENCE 1
#define NR_HAS_NGX_HOOK_EVIDENCE 1
COMMAND_EVIDENCE
SRWLOCK g_command_state_lock=SRWLOCK_INIT;
CommandStateEvidence g_command_state_evidence;
COMMAND_SNAPSHOT
HOOK_EVIDENCE
SRWLOCK g_ngx_hook_failure_lock=SRWLOCK_INIT;
NgxHookFailureEvidence g_ngx_hook_failure;
HOOK_SNAPSHOT
std::atomic<unsigned long long> g_nr_successful_evaluations{0},g_nr_transfer_submitted_frames{0},g_carrier_evaluations{0},g_rr_passthrough_evaluations{0},g_feature1_preflight_covered{0},g_feature1_preflight_attempts{0},g_command_signature_create_calls{0},g_command_signature_unknown_uses{0};
std::atomic<unsigned> g_live_rr_handles{0},g_rr_create_in_progress{0};
std::atomic<int> g_last_bypass_reason{0},g_last_carrier_feature{1},g_module_observer_fault{0};
std::atomic<bool> g_generic_renodx_conflict{false},g_preflight_core_loaded{true},g_preflight_dlssd_loaded{false},g_command_signature_hook_published{true},g_command_signature_start_deferred{false};
enum class BypassReason{Unknown};enum class ModuleObserverFault{None};
const char *BypassReasonName(BypassReason){return "command-state-unverified";}
const char *ModuleObserverFaultName(ModuleObserverFault){return "none";}
bool OtherNrOwnerActive(){return false;}
ACTIVITY''')
old='    begin(runtime);check(nr_core_feedback::Busy(),"worker owned by current session");'
assert s.count(old)==1
s=s.replace(old,'''    g_command_state_evidence.available=true;g_command_state_evidence.sampled_at=GetTickCount64();
    g_command_state_evidence.reason="null-guard-awaiting-reset";g_command_state_evidence.token=42;
    g_command_state_evidence.sequence=3;g_command_state_evidence.guard_requested=true;
    g_command_state_evidence.guard_covered=true;g_command_state_evidence.registered=true;
    g_command_state_evidence.baseline="Reset";
    g_ngx_hook_failure.available=true;g_ngx_hook_failure.sampled_at=GetTickCount64();
    std::strcpy(g_ngx_hook_failure.operation,"core EvaluateFeature");
    g_ngx_hook_failure.status=8;g_ngx_hook_failure.entry="existing-short-hotpatch";
    AcquireSRWLockExclusive(&g_command_state_lock);
    check(!SnapshotCommandStateEvidence().available,"command evidence contention is nonblocking");
    ReleaseSRWLockExclusive(&g_command_state_lock);
    AcquireSRWLockExclusive(&g_ngx_hook_failure_lock);
    check(!SnapshotNgxHookFailure().available,"hook evidence contention is nonblocking");
    ReleaseSRWLockExclusive(&g_ngx_hook_failure_lock);
'''+old)
old='    save("valid-core.zip");';assert s.count(old)==1
s=s.replace(old,'''    check(std::strstr(nr_core_feedback::g_submission,"end.commandState.reason=null-guard-awaiting-reset"),"actual ticket includes command-state reason");
    check(std::strstr(nr_core_feedback::g_submission,"end.ngxHookFailure.entry=existing-short-hotpatch"),"actual ticket includes historical hook shape");
'''+old)
old='    values = {';assert s.count(old)==1
s=s.replace(old,"    command = (ROOT/'src/nr_command_state.inl').read_text(encoding='utf-8-sig')\n    values = {\n        'COMMAND_EVIDENCE': definition(command, 'struct CommandStateEvidence', True),\n        'COMMAND_SNAPSHOT': definition(command, 'CommandStateEvidence SnapshotCommandStateEvidence('),\n        'HOOK_EVIDENCE': definition(source, 'struct NgxHookFailureEvidence', True),\n        'HOOK_SNAPSHOT': definition(source, 'NgxHookFailureEvidence SnapshotNgxHookFailure('),\n        'ACTIVITY': definition(legacy, 'void FeedbackAppendActivity('),")
old="    print('Actual Windows core ZIPs independently verified";assert s.count(old)==1
s=s.replace(old,'''    import zipfile
    with zipfile.ZipFile(out/'fixtures/valid-core.zip') as archive:
        metadata = archive.read('capture-info.txt').decode('utf-8-sig')
        for key in ('start.commandState.schema=1', 'end.commandState.token=42',
                    'end.commandState.reason=null-guard-awaiting-reset',
                    'end.commandState.guardCovered=1', 'end.commandState.freshReset=0',
                    'end.ngxHookFailure.entry=existing-short-hotpatch'):
            assert key in metadata, key
'''+old)
write(p,s)
