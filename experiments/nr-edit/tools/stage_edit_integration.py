#!/usr/bin/env python3
"""Add #258/#259 AFTER the target branch's generator, without changing checkout.
All anchors are validated before any candidate directory is created. Version and
all unrelated D19/D20 code survive; we only add an edit-v4 build identity suffix.
"""
from __future__ import annotations
import argparse,hashlib,importlib.util,json,re,shutil,tempfile,difflib
from pathlib import Path
PACKAGE=Path(__file__).resolve().parents[1]
sp=importlib.util.spec_from_file_location('private_image',PACKAGE/'tools/stage_private_image.py')
private=importlib.util.module_from_spec(sp);sp.loader.exec_module(private)
once,definition,span=private.once,private.definition,private.span
OVERLAY=[
 'src/nr_private_sr_runtime.hpp','src/nr_private_sr_core_adapter.inl','src/nr_private_image_core.inl',
 'src/nr_edit_dx12.hpp','src/nr_edit_dx12.cpp','src/nr_rr_dx12.hpp','src/nr_rr_dx12.cpp',
 'src/nr_deferred_dx12.hpp','src/nr_deferred_dx12.cpp','src/nr_edit_flow.hpp','src/nr_edit_adapters.hpp',
 'src/nr_edit_policy.hpp','src/nr_edit_forward.inl','src/nr_edit_core_runtime.inl','src/nr_edit_settings_ui.inl',
 'tools/compile_edit_artifacts.py','tools/compile_edit_shader.cpp',
 'shaders/nr_normalize_guides.hlsl','shaders/nr_rr_edit.hlsl','shaders/nr_deferred_edit.hlsl','shaders/nr_edit_scalar.hlsli']
def change_definition(text,signature,transform):
 a,b,s=definition(text,signature);return text[:a]+transform(s)+text[b:]
def prepend_body(s,code):
 a=s.index('{')+1;return s[:a]+'\n'+code+'\n'+s[a:]
def add_parameter(s,param):
 a=s.index('(');b=span(s,a,'(',')');return s[:b-1]+', '+param+s[b-1:]
def patch_core(text):
 if 'NR_EDIT_INTEGRATION_V' in text:raise ValueError('Edit experiment already staged')
 text=private.patch(text)
 includes='''#define NR_EDIT_INTEGRATION_V4 1
#define NR_EDIT_PRIVATE_SR_BYPASS_COMPLETE 1
#include <array>
#include <optional>
#include <cstdlib>
#include "nr_edit_policy.hpp"
#include "nr_edit_adapters.hpp"
#include "../build/nr_edit_binaries.hpp"
'''
 text=once(text,'namespace {',includes+'\nnamespace {\n#include "nr_edit_forward.inl"')
 text=change_definition(text,'struct ChainConfig {',lambda s:prepend_body(s,'    nr_edit_policy::Settings edit_experiment{};'))
 for signature in ('bool SameConfig(', 'bool SameHistoryConfig('):
  text=change_definition(text,signature,lambda s:once(s,'return ','return a.edit_experiment==b.edit_experiment && '))
 def reload(s):
  at='    const bool changed = !SameConfig(next, g_config);'
  block='''    next.edit_experiment.mode=GetPrivateProfileIntW(L"NRBeforeSR",L"EditExperimentMode",0,g_ini_path);
    next.edit_experiment.budget_mib=GetPrivateProfileIntW(L"NRBeforeSR",L"EditExperimentBudgetMiB",512,g_ini_path);
    next.edit_experiment.rr_history=GetPrivateProfileIntW(L"NRBeforeSR",L"EditExperimentRrHistory",1,g_ini_path)!=0;
    const auto read_edit_float=[&](const wchar_t* key,float fallback){
        wchar_t buffer[64]{};GetPrivateProfileStringW(L"NRBeforeSR",key,L"",buffer,64,g_ini_path);
        wchar_t* end=nullptr;const float value=std::wcstof(buffer,&end);
        return end!=buffer&&end&&*end==0&&std::isfinite(value)?value:fallback;
    };
    next.edit_experiment.carrier_range=read_edit_float(L"EditExperimentCarrierRange",4.f);
    next.edit_experiment.history_rate=read_edit_float(L"EditExperimentHistoryRate",.08f);
    next.edit_experiment=nr_edit_policy::Sanitize(next.edit_experiment);
'''
  return once(s,at,block+at)
 text=change_definition(text,'void ReloadConfig(',reload)
 text=change_definition(text,'struct PerfSignature {',lambda s:s[:-1]+'    nr_edit_policy::Settings edit_experiment{};\n}')
 text=change_definition(text,'bool SamePerfSignature(',lambda s:once(s,'return ','return a.edit_experiment==b.edit_experiment && '))
 anchor='const PerfSignature current = {'
 if text.count(anchor)!=1:raise ValueError('PerfSignature initializer changed')
 a=text.index(anchor);op=text.index('{',a);end=private.end_bracket(text,op,'{','}')
 text=text[:end]+', perf_config.edit_experiment'+text[end:]
 text=change_definition(text,'bool SaveConfig(',lambda s:once(s,'    record_write(WriteIniInt(L"ConfigVersion", 4));','''    record_write(WriteIniInt(L"EditExperimentMode",config.edit_experiment.mode));
    record_write(WriteIniInt(L"EditExperimentBudgetMiB",config.edit_experiment.budget_mib));
    record_write(WriteIniInt(L"EditExperimentRrHistory",config.edit_experiment.rr_history?1:0));
    record_write(WriteIniFloat(L"EditExperimentCarrierRange",config.edit_experiment.carrier_range));
    record_write(WriteIniFloat(L"EditExperimentHistoryRate",config.edit_experiment.history_rate));
    record_write(WriteIniInt(L"ConfigVersion", 4));'''))
 def private_image(s):
  s=re.sub(r'\bg_config\b','frame_config',s)
  anchor='    const bool private_image=request!=nullptr;'
  s=once(s,anchor,anchor+'''
    ChainConfig frame_config=g_config;
    if(private_image){frame_config.transfer_strength=ActiveTransferStrength(g_config);frame_config.mode=2;}
''')
  return s.replace(', private_image ? 2 : 0)',', private_image ? 2 : 0, private_image ? &frame_config : nullptr)')
 text=change_definition(text,'Substitution PrepareNrPrivateOrBefore(',private_image)
 def dispatch(s):
  s=add_parameter(s,'const ChainConfig* requested_frame_config = nullptr')
  return once(s,'ChainConfig frame_config = g_config;','ChainConfig frame_config = requested_frame_config ? *requested_frame_config : g_config;')
 text=change_definition(text,'void DispatchCodec(',dispatch)
 text=text.replace('nr_processing_runtime::MarkRecorded();','if(!nr_edit_core::PrivateImageActive())nr_processing_runtime::MarkRecorded();')
 text=change_definition(text,'void CollectF8GpuLocked(',lambda s:prepend_body(s,'    nr_edit_core::CollectLocked();'))
 text=change_definition(text,'void DrainPendingContextDestructions(',lambda s:prepend_body(s,'    nr_edit_core::DrainRetirements();'))
 def completion(s):
  anchor='        --record->in_flight_submissions;'
  return once(s,anchor,'        nr_edit_core::SubmitLocked(record->token,queue_state,batch.fence_value);\n'+anchor)
 text=change_definition(text,'void CompleteSubmissionBatchLocked(',completion)
 def unsubmitted(s):
  anchor='    for (unsigned int i = 0; i < record->context_count; ++i) {'
  return once(s,anchor,'    nr_edit_core::InvalidateLocked(token,true);\n'+anchor)
 text=change_definition(text,'bool QuarantineUnsubmittedLifecycleRecordLocked(',unsubmitted)
 hooks=[('NgxResult HookCreate(','g_original_create','command_list, feature_id, params, out_handle'),
  ('NgxResult HookCoreCreate(','g_original_core_create','command_list, feature_id, params, out_handle'),
  ('NgxResult HookDlssdCreate(','g_original_dlssd_create','command_list, feature_id, params, out_handle'),
  ('NgxResult HookEvaluate(','g_original_evaluate','command_list, handle, params, callback'),
  ('NgxResult HookCoreEvaluate(','g_original_core_evaluate','command_list, handle, params, callback'),
  ('NgxResult HookDlssdEvaluate(','g_original_dlssd_evaluate','command_list, handle, params, callback'),
  ('NgxResult HookRelease(','g_original_release','handle'),('NgxResult HookCoreRelease(','g_original_core_release','handle'),
  ('NgxResult HookDlssdRelease(','g_original_dlssd_release','handle')]
 for sig,fn,args in hooks:text=change_definition(text,sig,lambda s,fn=fn,args=args:prepend_body(s,f'    if(nr_edit_core::PrivateSrCall())return {fn}({args});'))
 text=change_definition(text,'NgxResult DispatchEvaluate(',lambda s:prepend_body(s,'    if(nr_edit_core::PrivateSrCall())return original(command_list,handle,params,callback);'))
 def process(s):
  anchor='    nr_processing_runtime::CarrierScope processing_carrier;'
  return once(s,anchor,anchor+'''
    NgxResult edit_result=0;
    if(nr_edit_core::TryProcess(original,command_list,handle,params,callback,feature,edit_result)){
        LeaveCriticalSection(&g_chain_cs);rr_transaction.Release();return edit_result;
    }
''')
 text=change_definition(text,'NgxResult ProcessEvaluate(',process)
 if '#include "interop/nr_rr_post_runtime.inl"' in text:
  text=once(text,'#include "interop/nr_rr_post_runtime.inl"','#include "nr_edit_core_runtime.inl"\n#include "interop/nr_rr_post_runtime.inl"')
 else:
  a,_,_=definition(text,'NgxResult ProcessRrPost(');text=text[:a]+'#include "nr_edit_core_runtime.inl"\n'+text[a:]
  text=change_definition(text,'NgxResult ProcessRrPost(',patch_rr_function)
 pattern=r'(constexpr const char \*kBuildIdentity = ")([^"\n]+)(";)'
 matches=list(re.finditer(pattern,text))
 if len(matches)!=1:raise ValueError('Build identity assignment changed')
 return re.sub(pattern,lambda m:m[1]+m[2]+'-edit-v4'+m[3],text,count=1)
def patch_rr_function(s):
 if s.count('nr_post_settings::Read().rr')!=2:raise ValueError('RR enablement gates changed; expected entry and locked eligibility checks')
 s=s.replace('nr_post_settings::Read().rr','(nr_post_settings::Read().rr || nr_edit_policy::Selected(g_config.edit_experiment,13))')
 anchor='    nr_processing_runtime::CarrierScope processing_carrier;'
 return once(s,anchor,anchor+'''
    NgxResult edit_result=0;
    if(nr_edit_core::TryProcess(original,command_list,handle,params,callback,feature,edit_result)){
        LeaveCriticalSection(&g_chain_cs);ownership.Release();return edit_result;
    }
''')
def patch_processing(s):
 def change(body):
  if 'g_config.transfer_strength' not in body:raise ValueError('Work enabled expression changed')
  return body.replace('g_config.transfer_strength','(g_config.edit_experiment.mode && g_config.mode==1 ? g_config.post_transfer_strength : g_config.transfer_strength)')
 return change_definition(s,'void Reconcile(',change)
def patch_ui(s,unified=False):
 s=once(s,'#include "nr_light_style_ui.inl"','#include "nr_edit_settings_ui.inl"\n#include "nr_light_style_ui.inl"')
 if unified:
  if s.count('#include "nr_unified_settings.inl"')!=1:raise ValueError('Unified settings owner changed')
  return s
 return once(s,'    bool changed = false;','    bool changed = false;\n    changed |= DrawEditExperimentSettings(next);')
def patch_unified_pages(s):
 anchor='void DrawUnifiedExperiments(ChainConfig& next,unsigned generation,bool pending,bool& changed,bool& discrete) {'
 return once(s,anchor,anchor+'\n    if(DrawEditExperimentSettings(next)){changed=true;discrete=true;}')
def patch_build(s,source_commit):
 if not re.fullmatch(r'[0-9a-f]{40}',source_commit):raise ValueError('Invalid source commit')
 marker="$Common += '-DNR_CODEC_PRECOMPILED=1'"
 insertion='''
# Independent shader compiler preserves finite checks; base codecs stay unchanged.
$EditCompiler=Join-Path $Build 'compile-edit-shader.exe'
& $CppCompiler '-std=c++20' '-O2' '-static' '-municode' (Join-Path $ProjectRoot 'tools/compile_edit_shader.cpp') '-ld3dcompiler' '-o' $EditCompiler
if ($LASTEXITCODE -ne 0) { throw 'Edit shader compiler build failed.' }
& python (Join-Path $ProjectRoot 'tools/compile_edit_artifacts.py') --compiler $EditCompiler --source (Join-Path $ProjectRoot 'shaders') --output $Build
if ($LASTEXITCODE -ne 0) { throw 'Edit shader compilation failed.' }
foreach ($name in @('nr_edit_dx12','nr_rr_dx12','nr_deferred_dx12')) {
    $source=Join-Path $ProjectRoot ('src/'+$name+'.cpp')
    $object=Join-Path $Objects ($name+'.o')
    & $CppCompiler @Common '-std=c++20' '-Wall' '-Wextra' '-Werror' '-c' $source '-o' $object
    if ($LASTEXITCODE -ne 0) { throw "Edit backend compilation failed: $name" }
    $ObjectFiles += $object
}
'''
 s=once(s,marker,marker+'\n'+insertion)
 begin=s.find('$SourceCommit = ');end=s.find('$Common = ',begin)
 if begin<0 or end<0:raise ValueError('Build source identity block changed')
 s=s[:begin]+f"$SourceCommit = '{source_commit}'\n$SourceDirty = '1' # See exact edit-integration-manifest.json\n\n"+s[end:]
 if not s.startswith('param('):raise ValueError('Top-level build parameter block changed')
 s=s.replace('param(','param(\n    [switch]$SkipUnchangedNrChain,',1)
 begin=s.find("& (Join-Path $PSScriptRoot 'build-nrchain.ps1')");end=s.find('$ResourceSource',begin)
 if begin<0 or end<0:raise ValueError('Build nrchain block changed')
 s=s[:begin]+'if (-not $SkipUnchangedNrChain) {\n'+s[begin:end]+'}\n\n'+s[end:]
 s=once(s,'foreach ($Output in $AddonOutputs + $BridgeOutput) {',
  "$AuditOutputs = @($AddonOutputs)\nif (-not $SkipUnchangedNrChain) { $AuditOutputs += $BridgeOutput }\nforeach ($Output in $AuditOutputs) {")
 s=once(s,'foreach ($AddonBuild in $AddonBuilds) {',"foreach ($AddonBuild in $AddonBuilds) {\n    $AddonBuild.Output = $AddonBuild.Output -replace '\\.addon64$', '-edit-v4.addon64'")
 return s

def digest(p:Path):return hashlib.sha256(p.read_bytes()).hexdigest()
def stage(source:Path,output:Path,source_commit:str):
 source=source.resolve(strict=True);output=output.resolve()
 if not re.fullmatch(r'[0-9a-f]{40}',source_commit):raise ValueError('Real 40-character source commit required')
 if output==source or source in output.parents or output in source.parents:raise ValueError('Use separate staging directory, never the active tree')
 if output.exists():raise FileExistsError('Refusing to replace existing staging tree')
 required=['src/nr_before_sr.cpp','src/nr_settings_ui.inl','src/nr_processing_runtime.inl','scripts/build.ps1']
 for f in required:(source/f).resolve(strict=True)
 texts={r:(source/r).read_text(encoding='utf-8-sig') for r in required}
 version=re.search(r'constexpr const char \*kAddonVersion = "([^"\n]+)";',texts[required[0]])
 if not version:raise ValueError('Cannot establish original product version')
 unified=(source/'src/nr_unified_pages.inl').is_file()
 changed={required[0]:patch_core(texts[required[0]]),required[1]:patch_ui(texts[required[1]],unified),
  required[2]:patch_processing(texts[required[2]]),required[3]:patch_build(texts[required[3]],source_commit)}
 if unified:
  r='src/nr_unified_pages.inl';texts[r]=(source/r).read_text(encoding='utf-8-sig');changed[r]=patch_unified_pages(texts[r])
 if '#include "interop/nr_rr_post_runtime.inl"' in changed[required[0]]:
  r='src/interop/nr_rr_post_runtime.inl';texts[r]=(source/r).read_text(encoding='utf-8-sig');changed[r]=patch_rr_function(texts[r])
 allow_suffix={'.cpp','.c','.h','.hpp','.inl','.hlsl','.hlsli','.def','.rc','.manifest','.json','.py','.ps1','.ini','.txt','.md','.cmake'}
 input_files=[]
 for directory in ['src','scripts','config','third_party','tools','shaders','include']:
  root=source/directory
  if root.exists():
   for p in root.rglob('*'):
    if p.is_symlink():raise ValueError('Symlink input refused: '+str(p))
    if p.is_file() and p.suffix.lower() in allow_suffix:input_files.append(p)
 before={str(p.relative_to(source)).replace('\\','/'):digest(p) for p in input_files}
 overlay_hash={rel:digest(PACKAGE/rel) for rel in OVERLAY}
 # Independent model/present/descriptor modules outside the changed generated
 # files must be byte-identical in the candidate.
 output.parent.mkdir(parents=True,exist_ok=True);temporary=Path(tempfile.mkdtemp(prefix='nr-edit-stage-',dir=output.parent))
 try:
  for p in input_files:
   to=temporary/p.relative_to(source);to.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,to)
  for rel in OVERLAY:
   to=temporary/rel;to.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(PACKAGE/rel,to)
  for rel,text in changed.items():
   p=temporary/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8-sig' if p.suffix=='.ps1' else 'utf-8')
  after={str(p.relative_to(source)).replace('\\','/'):digest(p) for p in input_files}
  if before!=after:raise RuntimeError('Source changed during staging; discard and retry after local work is frozen')
  if overlay_hash!={rel:digest(PACKAGE/rel) for rel in OVERLAY}:raise RuntimeError('Experiment changed during staging')
  preserved=[r for r in before if r not in changed and r not in OVERLAY]
  for r in preserved:
   if digest(temporary/r)!=before[r]:raise RuntimeError('Unrelated target source changed: '+r)
  manifest={'schema':'nr-edit-core-integration-v4','base_version':version[1],'upstream_commit':source_commit,'source_dirty':True,
   'scope':['258','259'],'present_257':'not-developed','sparse_nr2':'not-installed','input_files':before,
   'modified_files':{rel:digest(temporary/rel) for rel in changed},'overlay_files':overlay_hash,
   'preserved_files':preserved,'windows_compile':'not-run','hlsl_compile':'not-run','gpu_execution':'not-run','game_validation':'not-run'}
  (temporary/'edit-integration-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
  patch=''.join(''.join(difflib.unified_diff(texts[r].splitlines(True),text.splitlines(True),fromfile='generated/'+r,tofile='candidate/'+r)) for r,text in changed.items())
  (temporary/'edit-integration.diff').write_text(patch,encoding='utf-8');temporary.rename(output)
 except BaseException:
  shutil.rmtree(temporary,ignore_errors=True);raise
 return manifest

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--generated-root',type=Path,required=True)
 p.add_argument('--output',type=Path,required=True);p.add_argument('--source-commit',required=True)
 a=p.parse_args();m=stage(a.generated_root,a.output,a.source_commit)
 print(json.dumps({'output':str(a.output.resolve()),'base_version':m['base_version'],'modified':list(m['modified_files']),
  'preserved_count':len(m['preserved_files']),'status':'staged; full build/test not yet run'},ensure_ascii=False,indent=2))
if __name__=='__main__':main()
