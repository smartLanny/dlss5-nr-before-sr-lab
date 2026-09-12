// Project-owned, optional OF Feeder effect. Neural work remains inside Core V1.
#include "ReShade.fxh"
#ifndef RESHADE_DEPTH_INPUT_IS_REVERSED
#define RESHADE_DEPTH_INPUT_IS_REVERSED 0
#endif
texture2D NRGuideDepthInput : DEPTH;
sampler2D NRGuideDepthSampler { Texture=NRGuideDepthInput; MinFilter=POINT; MagFilter=POINT; MipFilter=POINT; };
texture2D NRGuideColor { Width=BUFFER_WIDTH; Height=BUFFER_HEIGHT; Format=RGBA8; };
texture2D NRGuideDepth { Width=BUFFER_WIDTH; Height=BUFFER_HEIGHT; Format=R32F; };
texture2D NRGuideFlow { Width=BUFFER_WIDTH; Height=BUFFER_HEIGHT; Format=RG32F; };
texture2D NRGuideResult { Width=BUFFER_WIDTH; Height=BUFFER_HEIGHT; Format=RGBA8; };
sampler2D NRGuideColorSampler { Texture=NRGuideColor; MinFilter=POINT; MagFilter=POINT; };
sampler2D NRGuideDepthCopy { Texture=NRGuideDepth; MinFilter=POINT; MagFilter=POINT; };
sampler2D NRGuideMotion { Texture=NRGuideFlow; MinFilter=POINT; MagFilter=POINT; };
sampler2D NRGuideResultSampler { Texture=NRGuideResult; MinFilter=POINT; MagFilter=POINT; };
uniform int NRGuideView=0;
uniform int NRGuideReady=0;
uniform int NRGuideCaptured=0;
uniform int NRGuideDepthAvailable=0;
uniform int NRGuideOutputReady=0;
uniform int NRGuideDepthReversed=RESHADE_DEPTH_INPUT_IS_REVERSED;
struct Captured { float4 color:SV_Target0; float depth:SV_Target1; };
Captured Capture(float4 position:SV_Position,float2 uv:TEXCOORD) {
    Captured r;
    r.color=tex2Dlod(ReShade::BackBuffer,float4(uv,0,0));
    r.depth=tex2Dlod(NRGuideDepthSampler,float4(uv,0,0)).x;
    return r;
}
float4 Apply(float4 position:SV_Position,float2 uv:TEXCOORD):SV_Target {
    return NRGuideOutputReady!=0 ? tex2D(NRGuideResultSampler,uv)
                                : tex2D(ReShade::BackBuffer,uv);
}
float4 Preview(float4 position:SV_Position,float2 uv:TEXCOORD):SV_Target {
    // Wrong effect order must never show a previous capture as the original.
    if(NRGuideCaptured==0)return tex2D(ReShade::BackBuffer,uv);
    if(NRGuideView==2) {
        if(NRGuideDepthAvailable==0)return float4(0.3,0,0.3,1);
        float d=tex2D(NRGuideDepthCopy,uv).x;
        // Raw hardware depth: reversed/logarithmic/clear semantics are NOT guessed.
        return float4(d,d,d,1);
    }
    if(NRGuideView==1 && NRGuideReady!=0) {
        float2 v=tex2D(NRGuideMotion,uv).xy;
        return float4(0.5+0.5*tanh(v.x/16),0.5+0.5*tanh(v.y/16),saturate(length(v)/32),1);
    }
    return tex2D(NRGuideColorSampler,uv);
}
technique NRGuideCapture < ui_label="NR guide capture (controlled by experiment addon)"; > {
    pass { VertexShader=PostProcessVS; PixelShader=Capture; RenderTarget0=NRGuideColor; RenderTarget1=NRGuideDepth; }
}
technique NRGuideApply < ui_label="NR result apply (controlled by experiment addon)"; > {
    pass { VertexShader=PostProcessVS; PixelShader=Apply; }
}
technique NRGuidePreview < ui_label="NR guide preview (diagnostic, not NR output)"; > {
    pass { VertexShader=PostProcessVS; PixelShader=Preview; }
}
