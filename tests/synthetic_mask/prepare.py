"""Generate test-only CombineV2 injection. Never edits production source or runs GPU."""
import hashlib, json, pathlib, re, sys
root=pathlib.Path(__file__).resolve().parents[2]
out=pathlib.Path(sys.argv[1]); out.mkdir(parents=True,exist_ok=True)
src=(root/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
h=(root/'tests/codec_gpu_harness.cpp.in').read_text(encoding='utf-8')
def one(s,a,b):
    if s.count(a)!=1: raise RuntimeError('nonunique anchor: '+a[:80])
    return s.replace(a,b)
shader=re.search(r'const char kCodecShader\[\] = R"HLSL\([\s\S]*?\)HLSL";',src)[0]
const=re.search(r'struct CodecConstants \{[\s\S]*?\n\};',src)[0]
desc=re.search(r'constexpr UINT kCodecSrvCount[\s\S]*?constexpr UINT kCodecRootConstantCount = \d+;',src)[0]
const=one(const,'\n};','\n    float synthetic_face_strength;\n};')
desc=one(desc,'kCodecRootConstantCount = 31','kCodecRootConstantCount = 32')
shader=one(shader,'    uint CaptureRoiHeight;','    uint CaptureRoiHeight;\n    float SyntheticFaceStrength;')
shader=one(shader,'Texture2D<float2> MotionSource  : register(t3);','Texture2D<float2> MotionSource  : register(t3);\nTexture2D<float4> SyntheticMask : register(t3);')
# This entry does not read MotionSource. Reuse the null test slot, not a shipping binding.
entry=shader.index('void CombineV2CS('); end=shader.index('void CopyCS(',entry)
body=shader[entry:end].replace('TransferStrength','localStrength')
anchor='    bool finiteInputs ='
body=one(body,anchor,'''    // Center mapping to a 2x2 SyntheticMask, clamped nonnegative bilinear weights.
    float2 q=(float2(tid.xy)+0.5)*2.0/float2(Width,Height)-0.5;
    int2 p=int2(floor(q)); float2 f=frac(q);
    float a=SyntheticMask.Load(int3(clamp(p,int2(0,0),int2(1,1)),0)).x;
    float b=SyntheticMask.Load(int3(clamp(p+int2(1,0),int2(0,0),int2(1,1)),0)).x;
    float c=SyntheticMask.Load(int3(clamp(p+int2(0,1),int2(0,0),int2(1,1)),0)).x;
    float d=SyntheticMask.Load(int3(clamp(p+1,int2(0,0),int2(1,1)),0)).x;
    float probability=lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
    float localStrength=lerp(TransferStrength,SyntheticFaceStrength,probability);
    bool finiteInputs =''')
shader=shader[:entry]+body+shader[end:]
h=one(h,'// @PRODUCTION_SHADER@',shader)
h=one(h,'// @PRODUCTION_CONSTANTS@',const)
h=one(h,'// @PRODUCTION_DESCRIPTOR_CONSTANTS@',desc)
h=one(h,'// @PRODUCTION_DESCRIPTOR_TESTS@','')
h=one(h,'        } else {\n            for (UINT index', '        } else {\n            throw std::runtime_error("WARP required; hardware fallback forbidden");\n            for (UINT index')
h=one(h,'struct CaptureRoi {','float synthetic_face=0;\nstd::vector<Float4> synthetic_mask(4,Float4{0,0,0,0});\nstruct CaptureRoi {')
h=one(h,'    auto source_texture =','    auto mask_texture=gpu.MakeTexture(2,2,synthetic_mask);\n    gpu.TextureState(mask_texture,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);\n    auto source_texture =')
h=one(h,'    gpu.Srv(3, nullptr);','    gpu.Srv(3, mask_texture.resource.p);')
h=one(h,'roi.width, roi.height};','roi.width, roi.height, synthetic_face};')
# Nonuniform channel gains exercise both luminance and chroma; not a pure luma fixture.
h=one(h,'Float4{0.3f, 0.45f, 0.6f, 0.875f}','Float4{0.32f, 0.40f, 0.53f, 0.875f}')
h=h[:h.index('int main()')]+(root/'tests/synthetic_mask/main.inl').read_text(encoding='utf-8')
(out/'synthetic_mask.cpp').write_text(h,encoding='utf-8')
files=['src/nr_before_sr.cpp','tests/codec_gpu_harness.cpp.in','tests/synthetic_mask/prepare.py','tests/synthetic_mask/main.inl']
(out/'manifest.json').write_text(json.dumps({'synthetic':True,'production_modified':False,'inputs':{f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files},'generated_sha256':hashlib.sha256((out/'synthetic_mask.cpp').read_bytes()).hexdigest()},indent=2))
