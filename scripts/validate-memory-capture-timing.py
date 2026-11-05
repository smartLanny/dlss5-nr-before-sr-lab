"""Compile actual combined timing/Active/window with counted CPU D3D12 seams."""
import argparse, ast, hashlib, json, pathlib, subprocess, time

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def once(s,a,b):
    if s.count(a)!=1: raise ValueError('Exact fixture anchor changed: '+a[:80])
    return s.replace(a,b,1)
def definition(s,marker):
    start=s.index(marker);brace=s.index('{',start);end=brace+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source-root','capture-fixture','compiler','output'):
        p.add_argument('--'+name,type=pathlib.Path,required=True)
    a=p.parse_args();root=a.source_root.resolve(strict=True)
    fixture=a.capture_fixture.resolve(strict=True);compiler=a.compiler.resolve(strict=True)
    inputs=[root/'src/nr_dual_timing.inl',root/'src/nr_private_sr_capture.inl',root/'src/nr_memory_observation.h',fixture]
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    module=ast.parse(fixture.read_text(encoding='utf-8-sig'))
    cpp=next(ast.literal_eval(n.value) for n in module.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='CPP' for t in n.targets))
    cpp=once(cpp,'struct Queue {','''#include "nr_memory_observation.h"
#define NR_HAS_MEMORY_OBSERVATION 1
nr_memory_observation::TimingWindow g_nr_memory_gpu_window;
struct Queue { unsigned long long identity=9;''')
    cpp=once(cpp,'Queue q{&fence,&queue};','Queue q{9,&fence,&queue};')
    cpp=once(cpp,'struct NrSlot {','''struct NrSlot {
 nr_memory_observation::TimingKey memory_timing_key{1,0,1,3,0,1280,720};''')
    cpp=once(cpp,'Check(time_samples==1&&second_completed==1,"CAPTURE_TIMING_OFF_SAMPLE");',
        '''Check(time_samples==1&&second_completed==1,"CAPTURE_TIMING_OFF_SAMPLE");
 Check(g_nr_memory_gpu_window.Read(1,3).count==1,"MEMORY_CAPTURE_NORMAL_COMPLETED");''')
    cpp=once(cpp,'Check(full_time[1].samples==1,"CAPTURE_TIMING_OFF_FULL_SAMPLE");',
        '''Check(full_time[1].samples==1,"CAPTURE_TIMING_OFF_FULL_SAMPLE");
 auto first=Ready(q,readback,heap,selected);first.dual_time_mask=3;first.dual_full_timing_layer=1;
 ResolveTiming(&list,first,true);CollectTiming(first);
 Check(g_nr_memory_gpu_window.Read(1,3).count==2,"MEMORY_CAPTURE_FULL_PRIMARY_COMPLETED");''')
    cpp=once(cpp,'const auto before_resolves=resolves,before_stamps=stamps,before_maps=maps;',
        '''const auto before_resolves=resolves,before_stamps=stamps,before_maps=maps;
  const auto before_memory=g_nr_memory_gpu_window.Read(1,3).count;''')
    cpp=once(cpp,'Check(time_samples==1&&full_time[1].samples==1,"CAPTURE_TIMING_NO_CAPTURE_SAMPLES");',
        '''Check(time_samples==1&&full_time[1].samples==1,"CAPTURE_TIMING_NO_CAPTURE_SAMPLES");
  Check(g_nr_memory_gpu_window.Read(1,3).count==before_memory,"MEMORY_CAPTURE_NO_LATE_OBSERVE");''')
    cpp=once(cpp,'Check(time_samples==2&&second_completed==2,"CAPTURE_TIMING_OLD_SAMPLE_RETAINED");',
        '''Check(time_samples==2&&second_completed==2,"CAPTURE_TIMING_OLD_SAMPLE_RETAINED");
 Check(g_nr_memory_gpu_window.Read(1,3).count==3,"MEMORY_CAPTURE_LAWFUL_OLD_SAMPLE");''')
    cpp=once(cpp,'Check(time_samples>=2,"CAPTURE_TIMING_RESUMED");',
        '''Check(time_samples>=2,"CAPTURE_TIMING_RESUMED");
 Check(g_nr_memory_gpu_window.Read(1,3).count==4,"MEMORY_CAPTURE_IDLE_RESUMED");''')
    timing=inputs[0].read_text(encoding='utf-8-sig')
    active=definition(inputs[1].read_text(encoding='utf-8-sig'),'bool Active()')
    cpp=cpp.replace('@ACTUAL_ACTIVE@',active)
    record={'schema':'actual-combined-memory-capture-timing-cpu-v1','passed':False,'gpu':False,'ngx':False,'game':False,
        'inputs_sha256':{str(x):sha(x) for x in inputs},'compiler_sha256':sha(compiler),'runner_sha256':sha(pathlib.Path(__file__)),
        'scope':'Actual production timing inl, capture Active and CPU TimingWindow; counted D3D12 API seams, no GPU/NGX execution. New normal/full-primary windows, four capture states, lawful old recording and idle resume.',
        'cases':[]}
    def save(): (out/'validation.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    save()
    variants=[('positive',None,None),('primary-guard','void ResolveTiming(','CAPTURE_TIMING_PRIMARY_EXCLUDED'),
        ('layer-guard','void ResolveLayerTiming(','CAPTURE_TIMING_FULL_EXCLUDED'),
        ('observe',None,'MEMORY_CAPTURE_NORMAL_COMPLETED')]
    for name,marker,expected in variants:
        directory=out/name;directory.mkdir();text=timing
        if marker:
            body=definition(text,marker)
            changed=once(body,'if(nr_private_sr_capture::Active()){','if(false){')
            text=once(text,body,changed)
        elif name=='observe':
            text=once(text,'g_nr_memory_gpu_window.Observe(key,ms[0]);','(void)key; // TEST ONLY omit normal observation')
        (directory/'nr_dual_timing.inl').write_text(text,encoding='utf-8')
        (directory/'nr_memory_observation.h').write_bytes(inputs[2].read_bytes())
        source=directory/'test.cpp';source.write_text(cpp,encoding='utf-8')
        binary=directory/'test.exe'
        command=[str(compiler),'-std=c++20','-O2','-static','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)]
        start=time.monotonic();compiled=subprocess.run(command,capture_output=True,timeout=90)
        (directory/'compile.log').write_bytes(compiled.stdout+compiled.stderr)
        case={'name':name,'compile_command':command,'compile_exit':compiled.returncode,'seconds':time.monotonic()-start,
            'generated_source_sha256':sha(source),'timing_sha256':sha(directory/'nr_dual_timing.inl'),'expected':expected}
        record['cases'].append(case);save()
        if compiled.returncode: print(name+' compile failed');return compiled.returncode
        run=subprocess.run([str(binary)],capture_output=True,timeout=20)
        log=run.stdout+run.stderr;(directory/'run.log').write_bytes(log)
        case.update(exit=run.returncode,binary_sha256=sha(binary),log_sha256=sha(directory/'run.log'))
        case['passed']=run.returncode==1 and ('FAIL '+expected).encode() in log if expected else run.returncode==0 and b'PASS dual capture timing' in log
        save();print(name+': '+log.decode('utf-8','replace').strip(),flush=True)
        if not case['passed']: return run.returncode or 1
    if any(sha(pathlib.Path(n))!=h for n,h in record['inputs_sha256'].items()): raise ValueError('Input changed during validation')
    record['passed']=True;save();return 0

if __name__=='__main__': raise SystemExit(main())
