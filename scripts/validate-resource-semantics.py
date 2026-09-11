#!/usr/bin/env python3
"""Run actual ResourceSemanticResolver policy and a false-Unique mutation control.

Copies only the two production headers into a fresh evidence directory before
compilation. The mutation is confined to that private copy; checkout files are
never edited. Device capabilities are supplied by a CPU test probe, not a GPU.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--source-root',type=Path,default=ROOT,
                        help='Repository or snapshot root containing src/nr_resource_semantics.h and src/interop/nr_format_access.h; flat two-header snapshots also accepted.')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    out=args.output.resolve()
    if out.exists():raise ValueError('Preserve prior evidence; choose a fresh --output.')
    out.mkdir(parents=True)
    source_root=args.source_root.resolve(strict=True)
    semantic=next((path for path in (source_root/'src/nr_resource_semantics.h',source_root/'nr_resource_semantics.h') if path.is_file()),None)
    if semantic is None:raise FileNotFoundError('nr_resource_semantics.h not found at --source-root')
    formats=next((path for path in (semantic.parent/'interop/nr_format_access.h',semantic.parent/'nr_format_access.h') if path.is_file()),None)
    if formats is None:raise FileNotFoundError('nr_format_access.h not found beside the semantic source')
    paths={'nr_resource_semantics.h':semantic,'interop/nr_format_access.h':formats}
    # A concurrently edited source must never produce an unlabelled mixed copy.
    contents={name:path.read_bytes() for name,path in paths.items()}
    if any(path.read_bytes()!=contents[name] for name,path in paths.items()):
        raise RuntimeError('Source changed while freezing the two-header snapshot; retry with a fresh output.')
    frozen=out/'source'
    for name,data in contents.items():
        target=frozen/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    fixture=ROOT/'tests/resource_semantics.cpp'
    frozen_fixture=out/'resource_semantics.cpp';frozen_fixture.write_bytes(fixture.read_bytes())
    compiler=args.compiler.resolve(strict=True)
    commands=[]
    def compile_and_run(include:Path,label:str,extra:tuple[str,...]=()) -> subprocess.CompletedProcess:
        binary=out/(label+'.exe')
        command=[str(compiler),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-static',
                 '-I'+str(include),str(frozen_fixture),'-o',str(binary)]
        commands.append(command)
        built=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
        (out/(label+'-compile.log')).write_text(built.stdout+built.stderr,encoding='utf-8')
        if built.returncode:
            print((built.stdout+built.stderr)[-12000:]);raise RuntimeError(label+' compilation failed')
        command=[str(binary),*extra];commands.append(command)
        ran=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=15)
        (out/(label+'.log')).write_text(ran.stdout+ran.stderr,encoding='utf-8')
        print(ran.stdout.strip())
        return ran
    positive=compile_and_run(frozen,'policy')

    # Deliberately reproduce the dangerous category error: eliminating legal
    # interpretations solely because their codecs are not implemented.
    original=contents['nr_resource_semantics.h'].decode('utf-8-sig')
    anchor='if (!c.contract_possible || !c.layout_legal) continue;'
    if original.count(anchor)!=1:raise RuntimeError('False-Unique mutation anchor changed; update the narrowly scoped inverse change.')
    mutant=original.replace(anchor,'if (!c.contract_possible || !c.layout_legal || !c.codec_implemented) continue;',1)
    negative_source=out/'false-unique-source'
    (negative_source/'interop').mkdir(parents=True)
    (negative_source/'nr_resource_semantics.h').write_text(mutant,encoding='utf-8')
    (negative_source/'interop/nr_format_access.h').write_bytes(contents['interop/nr_format_access.h'])
    negative=compile_and_run(negative_source,'false-unique',('--ambiguity-oracle',))
    negative_caught=negative.returncode==41 and 'SEMANTIC_AMBIGUITY_ORACLE' in negative.stdout
    passed=positive.returncode==0 and negative_caught
    sha=lambda data:hashlib.sha256(data).hexdigest()
    result={'status':'PASS' if passed else 'FAIL','actual_policy_direct_calls':True,
            'source_root':str(source_root),'sources':{name:sha(data) for name,data in contents.items()},
            'fixture_sha256':sha(frozen_fixture.read_bytes()),'compiler_sha256':sha(compiler.read_bytes()),
            'positive_exit':positive.returncode,'positive':positive.stdout.strip(),
            'false_unique_negative_caught':negative_caught,'negative_exit':negative.returncode,
            'negative_source_sha256':sha(mutant.encode('utf-8')),'commands':commands,
            'gpu':False,'ngx':False,'runtime_integration':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(('PASS' if passed else 'FAIL')+' ResourceSemanticResolver pure-policy and false-Unique control; no runtime/GPU claim')
    raise SystemExit(0 if passed else 1)

if __name__=='__main__':main()
