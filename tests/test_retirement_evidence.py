"""CPU evidence from production retirement functions; no GPU/NGX/game."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('retirement_definition',ROOT/'scripts/validate-aux-parent-retirement.py')
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)


class RetirementEvidence(unittest.TestCase):
    def test_production_retirement_and_read_only_f8(self):
        compiler=os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        llvm=os.environ.get('NR_LLVM_BIN')
        if llvm:compiler=str(Path(llvm)/('clang++.exe' if os.name=='nt' else 'clang++'))
        if not compiler:self.skipTest('C++ compiler unavailable')
        evidence=os.environ.get('DLSS5_RETIREMENT_TEST_EVIDENCE_DIR')
        if evidence:
            folder=Path(evidence);folder.mkdir(parents=True,exist_ok=False)
            self.run_fixture(compiler,folder)
        else:
            with tempfile.TemporaryDirectory(prefix='nr-retirement-evidence-') as directory:
                self.run_fixture(compiler,Path(directory))

    def run_fixture(self,compiler,folder):
        core=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
        binding=(ROOT/'src/nr_binding_runtime.inl').read_text(encoding='utf-8-sig')
        private=(ROOT/'src/nr_private_sr_product.inl').read_text(encoding='utf-8-sig')
        temporal=(ROOT/'src/temporal/recorded_temporal.cpp').read_text(encoding='utf-8-sig')
        temporal_header=(ROOT/'src/temporal/recorded_temporal.hpp').read_text(encoding='utf-8-sig')
        feedback=(ROOT/'src/nr_feedback_capture.inl').read_text(encoding='utf-8-sig')
        definition=helper.definition
        pieces={
            'predicate.inl':''.join(definition(core,m) for m in ('bool IsSlotFenceCompleteLocked(','bool IsContextFenceCompleteLocked(','bool CanMutateSlotLocked(','bool CanDestroyContextLocked(')),
            'collector.inl':''.join(definition(core,m) for m in ('void RetireAuxiliaryDependenciesLocked(','void CollectRetiredContextsLocked(','void DrainPendingContextDestructions(')),
            'bindings.inl':definition(binding,'bool CanDestroyBindingsLocked(')+definition(binding,'bool InvalidateSlotBindingsLocked(').replace('{','{++invalidations;',1),
            'runtime.inl':(ROOT/'src/nr_retirement_evidence_runtime.inl').read_text(encoding='utf-8-sig'),
            'private_inspector.inl':definition(private,'nr_retirement::OwnerInspection InspectRetirement('),
            'temporal_types.inl':definition(temporal_header,'struct RetirementFlight {',True)+definition(temporal_header,'struct RetirementInspection {',True),
            'temporal_inspector.inl':definition(temporal,'RetirementInspection RecordedTemporal::inspect_retirement('),
            'append.inl':definition(feedback,'void FeedbackAppend('),
            'formatter.inl':(ROOT/'src/nr_retirement_feedback.inl').read_text(encoding='utf-8-sig'),
        }
        report={'schema':'nr-retirement-evidence-cpu-v1','status':'running','gpu':False,'ngx':False,'game':False,
                'scope':'Production predicate/collector/binding/POD publisher/formatter and const inspectors; controlled OS and owner-maintenance services',
                'runs':[],'pieces_sha256':{name:hashlib.sha256(text.encode()).hexdigest() for name,text in pieces.items()}}
        flags=['-std=c++20','-O1','-Wall','-Wextra','-Werror','-fstack-usage']
        if os.name=='nt':flags+=['-static']
        mutations=(('candidate',None,None),
            ('binding-detail-overflow',None,None),
            ('binding-overflow-short-circuit','const auto lease=NrRetirementReadLease(packet.lease,tick,sample);',
             'binding detail overflow retains complete known lease and slot blockers'),
            ('binding-holder-from-details-only','indirect=indirect_binding_contexts[i]',
             'binding detail overflow retains actual indirect live-context list holder'),
            ('missing-pending-mask','if(c.pending_recording_token)row.blocked|=ContextPending;',
             'full mask preserves simultaneous pending/slot/binding/private/temporal blockers'),
            ('f8-extra-ready','out=g_nr_retirement_published;',
             'earlier branch never calls Ready or claims unobserved post-ready'))
        try:
            for name,marker,failure in mutations:
                case=folder/name;case.mkdir()
                small_detail=name in ('binding-detail-overflow','binding-overflow-short-circuit','binding-holder-from-details-only')
                for filename,text in pieces.items():
                    if filename=='runtime.inl' and small_detail:
                        self.assertEqual(text.count('if(out.bindings==kBindings)'),1)
                        text=text.replace('if(out.bindings==kBindings)','if(out.bindings==1)')
                    if filename=='runtime.inl' and marker:
                        self.assertEqual(text.count(marker),1)
                        replacement= ('if(out.bindings==1){++out.binding_overflow;continue;}'+marker if name=='binding-overflow-short-circuit' else
                            'indirect=0' if name=='binding-holder-from-details-only' else
                            '' if name=='missing-pending-mask' else 'nr_post_product::Ready(g_contexts[0]);'+marker)
                        text=text.replace(marker,replacement)
                    (case/filename).write_text(text,encoding='utf-8')
                exe=case/('retirement.exe' if os.name=='nt' else 'retirement')
                case_flags=['-DNR_RETIREMENT_SMALL_BINDING_DETAIL=1'] if small_detail else []
                commands=([compiler,*flags,*case_flags,'-I'+str(ROOT/'src'),'-I'+str(case),str(ROOT/'tests/retirement_evidence_cpu.cpp'),'-o',str(exe)],
                          [str(exe)])
                for index,command in enumerate(commands):
                    result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
                    (case/f'{index}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
                    report['runs'].append({'name':name,'index':index,'command':command,'exit':result.returncode})
                    expected=3 if index==1 and failure else 0
                    self.assertEqual(result.returncode,expected,result.stdout+result.stderr)
                    if index==1:
                        if failure:self.assertIn('FAIL '+failure,result.stderr)
                        else:print(result.stdout.strip(),flush=True)
                if name=='candidate':
                    stack={}
                    for receipt in case.glob('*.su'):
                        for line in receipt.read_text(encoding='utf-8').splitlines():
                            fields=line.split('\t')
                            if len(fields)>=3 and any(key in fields[0] for key in ('NrRetirement','FeedbackAppendRetirement','inspect_retirement','Clearv','main')):
                                stack[fields[0]]=int(fields[1])
                    for required in ('NrRetirementPublishLocked','SnapshotNrRetirementEvidence','FeedbackAppendRetirement'):
                        self.assertTrue(any(required in key for key in stack),required+' stack receipt missing')
                    self.assertLess(max(stack.values()),8192,'CPU fixture detected large diagnostic stack frame')
                    report['controlled_fixture_stack_bytes']=stack
                    report['stack_scope']='Clang O1 controlled CPU fixture; not an actual Core/game stack measurement'
            report['status']='passed'
        finally:(folder/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':unittest.main()
