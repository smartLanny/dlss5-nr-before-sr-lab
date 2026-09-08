#include "nr_skin_protection.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#endif

using namespace nr_skin_protection;
static unsigned checks = 0;
static void Require(bool condition, const char* label) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
}
static bool Bits(NrSkinRgb a, NrSkinRgb b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }
static bool Close(float a, float b, float tolerance = 3e-5f) {
    return std::abs(a - b) <= tolerance * std::max(1.0f, std::max(std::abs(a), std::abs(b)));
}
static NrSkinRequest Request(float luma, float colour) {
    auto r = NrSkinDefaultRequest(); r.enabled = true;
    r.preserveLuma = luma; r.preserveColour = colour; return r;
}
static InputIdentity Identity() {
    InputIdentity i;
    i.valid = true; i.encoding = ReferenceEncoding::Srgb709Display;
    i.origin = ReferenceOrigin::OriginalBeforeNr;
    i.exposureMatched = i.compositionLinear709 = true;
    i.sourceFrame = 37; i.sourceGeneration = 2; i.view = 4; i.exposureGeneration = 8; i.colourContractGeneration = 9;
    i.width = 1280; i.height = 720; i.rectX = 3; i.rectY = 7;
    i.rectWidth = 1101; i.rectHeight = 609; i.phaseX = .25f; i.phaseY = -.125f;
    return i;
}
static void Classification() {
    for (auto rgb : {NrSkinRgbValue(.76f,.49f,.36f), NrSkinRgbValue(.46f,.28f,.19f)}) {
        auto mask = NrSkinClassify(rgb, true);
        Require(mask.valid && mask.confidence > .5f, "synthetic skin-like seeds selected");
    }
    for (auto rgb : {NrSkinRgbValue(.1f,.3f,.8f), NrSkinRgbValue(.2f,.8f,.1f),
                    NrSkinRgbValue(1,0,0), NrSkinRgbValue(0,1,1),
                    NrSkinRgbValue(.8f,.8f,.8f), NrSkinRgbValue(.85f,.84f,.83f),
                    NrSkinRgbValue(.02f,.01f,.005f), NrSkinRgbValue(0,0,0)}) {
        auto mask = NrSkinClassify(rgb, true);
        Require(mask.valid && mask.confidence == 0.0f, "nonskin/gray/nearblack supported zero");
    }
    auto warmMaterial = NrSkinClassify(NrSkinRgbValue(.46f,.28f,.19f), true);
    Require(warmMaterial.confidence > .5f, "warm wood indistinguishable from same skin colour");
    Require(!NrSkinClassify(NrSkinRgbValue(.76f,.49f,.36f), false).valid, "unknown input rejected");
    for (auto rgb : {NrSkinRgbValue(-.1f,.3f,.2f), NrSkinRgbValue(2,.3f,.2f),
                    NrSkinRgbValue(std::numeric_limits<float>::quiet_NaN(),0,0),
                    NrSkinRgbValue(0,std::numeric_limits<float>::infinity(),0)})
        Require(!NrSkinClassify(rgb, true).valid, "negative/HDR-code/NaN input rejected");
    for (int r = 0; r <= 30; ++r) for (int g = 0; g <= 30; ++g) for (int b = 0; b <= 30; ++b) {
        auto mask = NrSkinClassify(NrSkinRgbValue(r/30.f,g/30.f,b/30.f), true);
        Require(mask.valid && std::isfinite(mask.confidence) &&
                mask.confidence >= 0 && mask.confidence <= 1, "bounded confidence cube");
    }
}
static void Admission() {
    auto original = Identity();
    Require(!AdmitReference(InputIdentity{}), "unknown metadata default rejected");
    Require(AdmitReference(original), "nonzero subrect original reference admitted");
    for (int field = 0; field < 19; ++field) {
        auto wrong = original;
        switch (field) {
        case 0: wrong.valid = false; break;
        case 1: wrong.encoding = ReferenceEncoding::Unknown; break;
        case 2: wrong.origin = ReferenceOrigin::NrOutput; break;
        case 3: wrong.exposureMatched = false; break;
        case 4: wrong.compositionLinear709 = false; break;
        case 5: ++wrong.sourceFrame; break;
        case 6: ++wrong.view; break;
        case 7: ++wrong.exposureGeneration; break;
        case 8: ++wrong.colourContractGeneration; break;
        case 9: ++wrong.width; break;
        case 10: ++wrong.height; break;
        case 11: ++wrong.rectX; break;
        case 12: ++wrong.rectY; break;
        case 13: --wrong.rectWidth; break;
        case 14: --wrong.rectHeight; break;
        case 15: wrong.phaseX = .5f; break;
        case 16: wrong.phaseY = .5f; break;
        case 17: wrong.phaseX = std::numeric_limits<float>::quiet_NaN(); break;
        case 18: ++wrong.sourceGeneration; break;
        }
        Require(!SameSource(wrong, original), "mismatched mask identity rejected");
    }
    auto invalidRect = original; invalidRect.rectWidth = std::numeric_limits<unsigned>::max();
    Require(!AdmitReference(invalidRect), "overflow rect rejected");
    MaskIdentity mask; mask.source = original; mask.calibrationGeneration = 3;
    Require(AdmitMask(mask, original, 3), "same source and calibration admitted");
    Require(!AdmitMask(mask, original, 4), "stale calibration rejected");
    NrSkinMask taps[] = {{0,true},{1,true},{0,false}};
    float weights[] = {.25f,.75f,0};
    auto mixed = WeightedMask(taps, weights, 3);
    Require(mixed.valid && mixed.confidence == .75f, "nonnegative confidence interpolation");
    weights[2] = .1f;
    Require(!WeightedMask(taps, weights, 3).valid, "invalid contributing tap rejects");
    weights[2] = -.1f;
    Require(!WeightedMask(taps, weights, 3).valid, "negative kernel rejects");
    weights[0] = weights[1] = weights[2] = 0;
    Require(!WeightedMask(taps, weights, 3).valid, "empty footprint rejects");
}
static void Endpoints() {
    auto original = NrSkinRgbValue(.40f,.18f,.08f);
    auto candidate = NrSkinRgbValue(.15f,.42f,.30f);
    auto full = NrSkinMaskValue(1,true);
    Require(!NrSkinDefaultRequest().enabled, "experiment defaults disabled");
    Require(Bits(NrSkinProtect(original,candidate,full,NrSkinDefaultRequest()).rgb,candidate), "disabled bit identity");
    Require(Bits(NrSkinProtect(original,candidate,full,Request(0,0)).rgb,candidate), "zero request bit identity");
    Require(NrSkinProtect(original,candidate,NrSkinMaskValue(0,true),Request(1,1)).reason == NrSkinZeroSelection,
            "valid zero selection has separate reason");
    Require(NrSkinProtect(original,candidate,NrSkinMaskValue(0,false),Request(1,1)).reason == NrSkinInvalidMask,
            "invalid mask has separate reason");
    for (auto mask : {NrSkinMaskValue(0,true), NrSkinMaskValue(1,false), NrSkinMaskValue(-1,true),
                     NrSkinMaskValue(2,true), NrSkinMaskValue(std::numeric_limits<float>::quiet_NaN(),true)})
        Require(Bits(NrSkinProtect(original,candidate,mask,Request(1,1)).rgb,candidate), "invalid/zero mask bit identity");
    Require(Bits(NrSkinProtect(original,candidate,full,Request(1,1)).rgb,original), "full exact original");
    auto colour = NrSkinProtect(original,candidate,full,Request(0,1));
    Require(colour.applied && Close(NrSkinLuma709(colour.rgb),NrSkinLuma709(candidate)), "colour-only preserves candidate luma");
    Require(Close(colour.rgb.r / colour.rgb.g, original.r / original.g), "colour-only restores original chromaticity");
    auto luma = NrSkinProtect(original,candidate,full,Request(1,0));
    Require(luma.applied && Close(NrSkinLuma709(luma.rgb),NrSkinLuma709(original)), "luma-only restores original luma");
    Require(Close(luma.rgb.r / luma.rgb.g, candidate.r / candidate.g), "luma-only preserves candidate chromaticity");
    for (float amount : {0.f,.25f,.5f,.75f,1.f}) {
        auto r = NrSkinProtect(original,original,full,Request(amount,1-amount));
        Require(Bits(r.rgb,original), "nonzero identity creates no edit");
    }
    for (auto bad : {NrSkinRgbValue(-1,0,0), NrSkinRgbValue(70000,1,2),
                    NrSkinRgbValue(std::numeric_limits<float>::quiet_NaN(),0,0)}) {
        Require(Bits(NrSkinProtect(bad,candidate,full,Request(1,1)).rgb,candidate), "invalid source no edit");
        Require(Bits(NrSkinProtect(original,bad,full,Request(1,1)).rgb,bad), "invalid candidate caller fallback retained");
    }
    for (auto r : {Request(-.1f,1), Request(1,2), Request(1,std::numeric_limits<float>::infinity())})
        Require(Bits(NrSkinProtect(original,candidate,full,r).rgb,candidate), "invalid request no edit");
    auto black = NrSkinRgbValue(0,0,0);
    Require(Bits(NrSkinProtect(black,candidate,full,Request(1,1)).rgb,black), "full black original exact");
    Require(Bits(NrSkinProtect(black,candidate,full,Request(.5f,.5f)).rgb,candidate), "undefined partial black chroma rejected");
    auto brightRed = NrSkinRgbValue(65504,0,0), brightCyan = NrSkinRgbValue(0,65504,65504);
    auto outside = NrSkinProtect(brightRed,brightCyan,full,Request(0,1));
    Require(!outside.applied && outside.reason == NrSkinOutputOutsideDomain && Bits(outside.rgb,brightCyan),
            "cross-component HDR overflow preserves candidate instead of clipping");
    auto greyA = NrSkinRgbValue(.2f,.2f,.2f), greyB = NrSkinRgbValue(.8f,.8f,.8f);
    auto grey = NrSkinProtect(greyA,greyB,full,Request(.5f,1));
    Require(Close(grey.rgb.r,.5f) && Close(grey.rgb.g,.5f) && Close(grey.rgb.b,.5f), "pure grey brightness only");
}
static void IndependentOracle() {
    std::mt19937 rng(208);
    std::uniform_real_distribution<float> channel(.001f,1000.f), weight(0,1);
    for (int i = 0; i < 20000; ++i) {
        auto s = NrSkinRgbValue(channel(rng),channel(rng),channel(rng));
        auto c = NrSkinRgbValue(channel(rng),channel(rng),channel(rng));
        float mask = weight(rng); auto request = Request(weight(rng),weight(rng));
        auto out = NrSkinProtect(s,c,NrSkinMaskValue(mask,true),request);
        Require(out.applied && NrSkinRgbInRange(out.rgb,65504), "finite HDR-scale output");
        const double ys = .2126*s.r + .7152*s.g + .0722*s.b;
        const double yc = .2126*c.r + .7152*c.g + .0722*c.b;
        const double a = double(mask)*request.preserveLuma, b = double(mask)*request.preserveColour;
        const double y = (1-a)*yc+a*ys;
        // Independent double-precision component oracle with convex weights.
        const float expected[] = {float(y*((1-b)*c.r/yc+b*s.r/ys)),
                                  float(y*((1-b)*c.g/yc+b*s.g/ys)),
                                  float(y*((1-b)*c.b/yc+b*s.b/ys))};
        Require(Close(out.rgb.r,expected[0]) && Close(out.rgb.g,expected[1]) && Close(out.rgb.b,expected[2]), "separable double oracle");
        Require(Close(NrSkinLuma709(out.rgb),float(y)), "luma convex endpoint budget");
    }
}
#ifdef _WIN32
static void CompileSm5(const char* headerPath) {
    std::ifstream input(headerPath, std::ios::binary); Require(bool(input), "open common shader source");
    std::ostringstream text; text << "#define NR_SKIN_HLSL 1\n" << input.rdbuf();
    text << R"HLSL(
StructuredBuffer<float4> Source : register(t0);
RWStructuredBuffer<float4> Result : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) {
    float4 input = Source[id.x];
    NrSkinRgb reference = NrSkinRgbValue(input.x,input.y,input.z);
    NrSkinMask mask = NrSkinClassify(reference,input.w > 0);
    NrSkinRequest request = NrSkinDefaultRequest();
    request.enabled = true; request.preserveLuma = .4; request.preserveColour = .7;
    NrSkinResult result = NrSkinProtect(reference,NrSkinRgbValue(.2,.4,.3),mask,request);
    Result[id.x] = float4(result.rgb.r,result.rgb.g,result.rgb.b,mask.valid ? mask.confidence : -1);
})HLSL";
    auto source = text.str(); ID3DBlob* code = nullptr; ID3DBlob* error = nullptr;
    HRESULT hr = D3DCompile(source.data(),source.size(),"nr_skin_protection.h",nullptr,nullptr,
        "main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error);
    if (error) { std::fwrite(error->GetBufferPointer(),1,error->GetBufferSize(),stderr); error->Release(); }
    Require(SUCCEEDED(hr) && code, "shared production scalar SM5 compilation");
    std::printf("PASS SM5 shared header: %zu shader bytes (compile only, no GPU dispatch)\n",code->GetBufferSize());
    code->Release();
}
#endif
int main(int argc, char** argv) {
    Classification(); Admission(); Endpoints(); IndependentOracle();
    if (argc == 3 && std::string(argv[1]) == "--sm5") {
#ifdef _WIN32
        CompileSm5(argv[2]);
#else
        Require(false, "SM5 requires Windows D3DCompiler");
#endif
    } else Require(argc == 1, "usage: test [--sm5 header]");
    std::printf("PASS skin protection: %u production checks; no runtime/game claims\n",checks);
}
