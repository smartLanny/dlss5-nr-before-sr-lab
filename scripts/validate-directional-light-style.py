#!/usr/bin/env python3
"""Validate the production whole-direction light selector against independent oracles.

CPU scalar arithmetic only. GPU tests separately cover actual stored pixels;
this runner does not claim HDR image, NVIDIA NR, game quality, or timing results.
Removing only the added selector/helper and its two calls must recover D4's
entire frozen shader exactly. The legacy V1 helper is not rewritten.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "ec338d7d9d69230f208b25e4fd457cc79d03c465"
DECLARATION = """float SelectLightStyleStrengthV1(bool wholeDirection, float remaining, float edit,
                                float mean, float master, float broad, float dark,
                                float reflection, float structure, float glow);
"""
CALL = "SelectLightStyleStrengthV1((Passthrough & 524288u) != 0u, "
DIRECTIONAL = r"// DIRECTIONAL_LIGHT_SCALAR_BEGIN\n(.*?)// DIRECTIONAL_LIGHT_SCALAR_END\n"

PREFIX = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
using std::abs; using std::isfinite;
static float min(float a,float b){return std::min(a,b);}
static float max(float a,float b){return std::max(a,b);}
static float clamp(float v,float a,float b){return std::clamp(v,a,b);}
static float saturate(float v){return std::clamp(v,0.f,1.f);}
static unsigned Passthrough=0, checks=0;
static void require(bool ok,const char *label){
    ++checks;if(!ok){std::fprintf(stderr,"DIRECTIONAL_LIGHT_ORACLE: %s\n",label);std::exit(23);}
}
static bool bits(float a,float b){return std::memcmp(&a,&b,sizeof(a))==0;}
static bool close(float a,double b){return std::abs(double(a)-b)<=2e-6*std::max(1.,std::abs(b));}
struct Controls {float broad,dark,reflection,structure,glow;};
static const Controls controls[]={
    {1,1,1,1,0},{2,.05f,1.91f,1.95f,1},{0,2,2,0,.3f},
    {2,.5f,0,2,0},{.5f,1.25f,1.5f,.75f,.7f},{2,2,2,2,0},{3,-1,3,-1,2}};
// Constructed inputs supply the partition; this oracle never calls a shader helper.
static double oracle(bool whole,float edit,float remaining,double partition,float master,Controls c){
    const double m=std::clamp(double(master),0.,4.);
    if(c.broad==1&&c.dark==1&&c.reflection==1&&c.structure==1&&c.glow==0)return m;
    if(edit==0||remaining==0||(edit>0)!=(remaining>0))return 0;
    const double b=edit>0?std::clamp(double(c.broad),0.,2.)*(1.-std::clamp(double(c.glow),0.,1.)):
                           std::clamp(double(c.dark),0.,2.);
    const double d=std::clamp(double(edit>0?c.reflection:c.structure),0.,2.);
    return std::clamp(whole?m*d*(partition*b+1.-partition):m*(partition*b+(1.-partition)*d),0.,4.);
}
'''

TESTS = r'''
int main(){
    for(float sign:{-1.f,1.f}) for(float magnitude:{1e-20f,1e-8f,.0625f,.5f,1.f,8.f})
    for(float fraction:{0.f,.125f,.5f,.875f,1.f}) for(float attenuation:{0.f,.25f,.75f,1.f})
    for(float master:{0.f,.5f,1.f,1.2f,2.f,4.f}) for(Controls c:controls){
        const float edit=sign*magnitude,remaining=edit*attenuation,mean=edit*fraction;
        const float legacy=LightStyleStrengthV1(remaining,edit,mean,master,c.broad,c.dark,c.reflection,c.structure,c.glow);
        require(bits(legacy,frozen::LightStyleStrengthV1(remaining,edit,mean,master,c.broad,c.dark,c.reflection,c.structure,c.glow)),
            "legacy V1 exact frozen D4 scalar");
        for(bool whole:{false,true}){
            const float actual=SelectLightStyleStrengthV1(whole,remaining,edit,mean,master,
                c.broad,c.dark,c.reflection,c.structure,c.glow);
            require(close(actual,oracle(whole,edit,remaining,fraction,master,c)),"independent partition/direction oracle");
            require(isfinite(actual)&&actual>=0&&actual<=4,"finite effective strength cap");
            require(remaining==0||remaining*(remaining*actual)>=0,"no sign reversal");
            require(remaining!=0||remaining*actual==0,"protected zero cannot be resurrected");
            if(!whole)require(bits(actual,legacy),"mode0 exact nonneutral D4 strength");
            if(c.broad==1&&c.dark==1&&c.reflection==1&&c.structure==1&&c.glow==0)
                require(bits(actual,legacy),"both modes neutral exact strength");
            Controls opposite=c;
            if(edit>0){opposite.dark=.37f;opposite.structure=1.63f;}
            else {opposite.broad=.37f;opposite.reflection=1.63f;opposite.glow=.41f;}
            const float isolated=SelectLightStyleStrengthV1(whole,remaining,edit,mean,master,
                opposite.broad,opposite.dark,opposite.reflection,opposite.structure,opposite.glow);
            require(close(remaining*actual,double(remaining)*isolated),"non-target sign isolated");
        }
    }
    // Same protected remainder, original partition and per-route ratio bound.
    for(bool whole:{false,true}) for(float edit:{-.25f,.25f})
    for(float fraction:{0.f,.5f,1.f}) for(float attenuation:{0.f,.25f,1.f})
    for(float master:{0.f,1.f,4.f}) for(float ratioLimit:{1.f,2.f,4.f,16.f}) for(Controls c:controls){
        Passthrough=whole?524288u:0u;
        const float remaining=edit*attenuation;
        const float actual=PostLightTransferRatioV1(remaining,edit,edit*fraction,master,ratioLimit,
            c.broad,c.dark,c.reflection,c.structure,c.glow);
        const double expected=std::clamp(1.+double(remaining)*oracle(whole,edit,remaining,fraction,master,c),
            1./ratioLimit,double(ratioLimit));
        require(close(actual,expected),"actual post linear transfer uses protected remainder");
        if(!whole)require(bits(actual,frozen::PostLightTransferRatioV1(remaining,edit,edit*fraction,master,ratioLimit,
            c.broad,c.dark,c.reflection,c.structure,c.glow)),"mode0 exact post transfer");
    }
    for(float invalid:{std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()})
    for(unsigned slot=0;slot<9;++slot){
        float v[]={.25f,.5f,.25f,1.f,2.f,2.f,2.f,2.f,.3f};v[slot]=invalid;
        require(WholeDirectionalLightStyleStrengthV1(v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8])==0,
            "whole mode rejects every nonfinite scalar");
    }
    require(WholeDirectionalLightStyleStrengthV1(-.1f,.5f,.25f,1,1,1,2,2,0)==0,"opposite remainder rejected");
    require(WholeDirectionalLightStyleStrengthV1(.1f,0,0,1,1,1,2,2,0)==0,"zero original cannot invent an edit");
    require(WholeDirectionalLightStyleStrengthV1(.5f,.5f,.5f,-1,2,2,2,2,0)==0,"negative master bounded");
    require(WholeDirectionalLightStyleStrengthV1(.5f,.5f,.5f,8,2,2,2,2,0)==4,"large master and directional product capped");
    for(float direction:{0.f,.5f,1.f,2.f}){
        require(WholeDirectionalLightStyleStrengthV1(.1f,.4f,.4f,1,2,1,direction,1,1)==0,
            "glow100 removes broad brightening at every direction gain");
        require(close(WholeDirectionalLightStyleStrengthV1(.1f,.4f,0,1,2,1,direction,1,1),direction),
            "glow100 retains fine positive direction");
        require(close(WholeDirectionalLightStyleStrengthV1(-.1f,-.4f,-.4f,1,2,.5f,2,direction,1),.5*direction),
            "negative broad gain remains independent of glow");
    }
    std::printf("PASS production directional light: %u numeric assertions; mode0 D4 bytes, whole/mixed signs, protection and scalar limits\n",checks);
}
'''


def section(source: str, name: str) -> str:
    matches = re.findall(r"// " + name + r"_BEGIN\n(.*?)// " + name + r"_END", source, re.S)
    if len(matches) != 1:
        raise ValueError(f"Expected one production {name} section")
    return matches[0]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--negative-control", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output and args.output.exists():
        raise ValueError("Output exists; preserve the prior evidence")
    source = (ROOT / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
    baseline = subprocess.check_output(["git", "show", BASELINE + ":src/nr_before_sr.cpp"],
                                       cwd=ROOT, text=True, encoding="utf-8")
    shader_pattern = r'const char kCodecShader\[\] = R"HLSL\([\s\S]*?\)HLSL";'
    shader = re.search(shader_pattern, source).group()
    old_shader = re.search(shader_pattern, baseline).group()
    if shader.count(DECLARATION) != 1 or shader.count(CALL) != 2:
        raise ValueError("Directional declaration or exact pre/post call count changed")
    restored, count = re.subn(DIRECTIONAL, "", shader, flags=re.S)
    restored = restored.replace(DECLARATION, "").replace(CALL, "LightStyleStrengthV1(")
    if count != 1 or restored != old_shader:
        raise ValueError("Changes outside the new helper and its two calls modified the frozen D4 shader")
    common = section(source, "FOUR_REGION_SCALAR")
    directional = section(source, "DIRECTIONAL_LIGHT_SCALAR")
    post = section(source, "POST_LIGHT_SCALAR")
    frozen = "namespace frozen {\n" + section(baseline, "FOUR_REGION_SCALAR") + section(baseline, "POST_LIGHT_SCALAR") + "}\n"
    variants = [("production", common, post)]
    if args.negative_control:
        expression = "return clamp(master * direction * (fraction * broadGain + (1.0 - fraction)),\n                 0.0, 4.0);"
        if directional.count(expression) != 1:
            raise ValueError("Expected one whole-direction cap mutation anchor")
        mutations = {
            "fine-only-instead-of-whole": directional.replace(expression,
                "return clamp(master * (fraction * broadGain + (1.0 - fraction) * direction),\n                 0.0, 4.0);"),
            "missing-whole-cap": directional.replace(expression,
                "return master * direction * (fraction * broadGain + (1.0 - fraction));"),
            "glow-removes-fine": directional.replace("? clamp(reflection, 0.0, 2.0) : clamp(structure, 0.0, 2.0);",
                "? clamp(reflection, 0.0, 2.0) * (1.0 - saturate(glow)) : clamp(structure, 0.0, 2.0);"),
            "reclassify-after-protection": directional.replace("LightBroadFractionV1(edit, mean)", "LightBroadFractionV1(remaining, mean)"),
            "ignore-mode-selector": directional.replace("if (wholeDirection)", "if (false && wholeDirection)"),
        }
        for name, changed in mutations.items():
            if changed == directional:
                raise ValueError(f"Mutation {name} did not change production")
            variants.append((name, common.replace(directional, changed), post))
        unprotected = post.replace("1.0 + remaining * strength", "1.0 + edit * strength")
        if unprotected == post:
            raise ValueError("Post protection mutation anchor missing")
        variants.append(("post-resurrects-protected-edit", common, unprotected))
    runs = []
    with tempfile.TemporaryDirectory(prefix="nr-directional-light-") as directory:
        for name, shared, post_code in variants:
            cpp = Path(directory) / (name + ".cpp")
            exe = cpp.with_suffix(".exe" if os.name == "nt" else "")
            cpp.write_text(PREFIX + frozen + shared + post_code + TESTS, encoding="utf-8")
            subprocess.run([args.compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                            str(cpp), "-o", str(exe)], check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            if name == "production":
                if run.returncode != 0:
                    raise RuntimeError(run.stdout + run.stderr)
                print(run.stdout.strip())
            elif run.returncode != 23 or "DIRECTIONAL_LIGHT_ORACLE:" not in run.stderr:
                raise RuntimeError(f"Unexpected negative-control result {name}: {run}")
            else:
                print(f"PASS compiled negative control {name}: {run.stderr.strip()}")
            runs.append({"name": name, "returncode": run.returncode, "stdout": run.stdout, "stderr": run.stderr})
    print("PASS whole shader reduces exactly to frozen D4 after removing only the selector addition")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        result = {"schema": "nr-directional-light-scalar-v1", "success": True,
                  "baseline_commit": BASELINE, "shader_sha256": hashlib.sha256(shader.encode()).hexdigest(),
                  "gpu_or_game_verified": False, "hdr_pixels_verified": False,
                  "legacy_shader_exact_after_removing_addition": True, "runs": runs}
        with args.output.open("x", encoding="utf-8") as out:
            json.dump(result, out, indent=2)
            out.write("\n")


if __name__ == "__main__":
    main()
