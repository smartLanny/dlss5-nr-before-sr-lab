#pragma once
// Current -> previous motion convention: NVIDIA DLSS Programming Guide,
// 31 March 2026, motion-vector section. Scales below convert bound NR vectors
// into normalized active-image coordinates, NOT texture-dimension guesses.
constexpr const char kNrFaceTrackerShader[] = R"hlsl(
Texture2D<float4> ReferenceProxy : register(t0);
Texture2D<float2> Motion : register(t1);
Texture2D<float> Depth : register(t2);
Texture2D<float4> CurrentGuide : register(t3);
Texture2D<float4> History0 : register(t4);
Texture2D<float4> History1 : register(t5);
Texture2D<float4> History2 : register(t6);
Texture2D<float4> History3 : register(t7);
Texture2D<float4> History4 : register(t8);
Texture2D<float4> History5 : register(t9);
Texture2D<float4> History6 : register(t10);
Texture2D<float4> History7 : register(t11);
RWTexture2D<float4> GuideOut : register(u0);
RWTexture2D<float4> FaceOut : register(u1);
RWByteAddressBuffer ReadbackOut : register(u2);
SamplerState LinearClamp : register(s0);
cbuffer FrameShape : register(b0) {
    uint Width; uint Height; uint ProxyWidth; uint ProxyHeight;
    uint MotionWidth; uint MotionHeight; uint DepthWidth; uint DepthHeight;
    float MotionScaleX; float MotionScaleY; uint FaceCount; uint FaceAge;
    uint HasPrevious; uint Reset; uint DepthValid; uint MotionValid;
    float JitterDeltaX; float JitterDeltaY; float DepthTolerance; uint Reserved;
    uint MotionX; uint MotionY; uint DepthX; uint DepthY;
};
struct FaceData { float4 box; float4 eyes; float4 noseMouth; float4 mouthScore; };
cbuffer Detections : register(b1) { FaceData Faces[16]; };
float4 History(uint step,float2 uv) {
    int3 pixel=int3(clamp(int2(uv*float2(Width,Height)),int2(0,0),int2(Width,Height)-1),0);
    switch(step) {
    case 0:return History0.Load(pixel);
    case 1:return History1.Load(pixel);
    case 2:return History2.Load(pixel);
    case 3:return History3.Load(pixel);
    case 4:return History4.Load(pixel);
    case 5:return History5.Load(pixel);
    case 6:return History6.Load(pixel);
    default:return History7.Load(pixel);
    }
}
uint Trailer(){return Width*Height*4;}
bool Inside(float2 uv){return all(uv>=0.0.xx)&&all(uv<=1.0.xx)&&all(isfinite(uv));}
bool Corresponds(float4 a,float4 b) {
    if(!all(isfinite(a))||!all(isfinite(b))||a.w<0||b.w<0)return false;
    // Conservative rejection, not a semantic classifier. Depth must have the
    // same projection contract, and exposure changes reset the whole history.
    return abs(a.z-b.z)<=DepthTolerance && abs(log2((a.w+.02)/(b.w+.02)))<1.25;
}
[numthreads(1,1,1)]
void FaceClearCS(uint3 id:SV_DispatchThreadID){ReadbackOut.Store4(Trailer(),uint4(0,0,0,0));}
[numthreads(8,8,1)]
void FaceGuideCS(uint3 id:SV_DispatchThreadID) {
    if(id.x>=Width||id.y>=Height)return;
    const float2 uv=(id.xy+.5)/float2(Width,Height);
    // Fixed 4x4 footprint bounds detection cost; it never reads enhanced NR.
    float3 rgb=0;
    [unroll]for(uint y=0;y<4;++y)[unroll]for(uint x=0;x<4;++x) {
        float2 sampleUv=(id.xy+(float2(x,y)+.5)/4)/float2(Width,Height);
        rgb+=ReferenceProxy.SampleLevel(LinearClamp,sampleUv,0).rgb/16;
    }
    bool valid=all(isfinite(rgb))&&DepthValid!=0&&MotionValid!=0;
    rgb=all(isfinite(rgb))?saturate(rgb):0.0.xxx;
    uint3 bgr=uint3(round(rgb.bgr*255));
    ReadbackOut.Store((id.y*Width+id.x)*4,bgr.x|(bgr.y<<8)|(bgr.z<<16));
    int2 mvPixel=min(int2(uv*float2(MotionWidth,MotionHeight)),int2(MotionWidth,MotionHeight)-1);
    int2 depthPixel=min(int2(uv*float2(DepthWidth,DepthHeight)),int2(DepthWidth,DepthHeight)-1);
    float2 motion=Motion.Load(int3(max(mvPixel,0)+int2(MotionX,MotionY),0))*float2(MotionScaleX,MotionScaleY)+float2(JitterDeltaX,JitterDeltaY);
    float depth=Depth.Load(int3(max(depthPixel,0)+int2(DepthX,DepthY),0));
    valid=valid&&all(isfinite(motion))&&isfinite(depth)&&depth>=0&&depth<=1&&all(abs(motion)<=1.0.xx);
    float4 guide=float4(motion,depth,valid?dot(rgb,float3(.2126,.7152,.0722)):-1.0);
    GuideOut[id.xy]=guide;
    bool consistent=valid&&Inside(uv+motion);
    if(HasPrevious!=0)consistent=consistent&&Corresponds(guide,History(0,uv+motion));
    uint unused;
    if(HasPrevious!=0&&!consistent)ReadbackOut.InterlockedAdd(Trailer(),1,unused);
    if(!valid)ReadbackOut.InterlockedAdd(Trailer()+4,1,unused);
}
float FaceWeight(float2 uv,FaceData face) {
    if(face.mouthScore.z<.6||face.box.z<=0||face.box.w<=0)return 0;
    float2 scale=float2(Width,Height);
    float2 eyes=(face.eyes.xy+face.eyes.zw)*.5*scale;
    float2 mouth=(face.noseMouth.zw+face.mouthScore.xy)*.5*scale;
    float2 axis=mouth-eyes;float axisLength=length(axis);
    if(axisLength<2)return 0;
    axis/=axisLength;
    float2 right=float2(axis.y,-axis.x);
    float2 centre=eyes+axis*(axisLength*.48);
    float2 delta=uv*scale-centre;
    float rx=max(face.box.z*Width*.46,2.0);
    float ry=max(axisLength*1.25,face.box.w*Height*.36);
    float radius=length(float2(dot(delta,right)/rx,dot(delta,axis)/ry));
    return (1-smoothstep(.78,1.0,radius))*smoothstep(.6,.75,face.mouthScore.z);
}
[numthreads(8,8,1)]
void FaceMaskCS(uint3 id:SV_DispatchThreadID) {
    if(id.x>=Width||id.y>=Height)return;
    float2 uv=(id.xy+.5)/float2(Width,Height);
    float4 current=CurrentGuide.Load(int3(id.xy,0));
    float mask=0;
    bool valid=Reset==0&&FaceCount>0&&FaceCount<=16&&FaceAge<=8&&current.w>=0;
    // A major image/depth discontinuity invalidates every old detection in
    // this very frame. The clear/guide/mask dispatches have explicit barriers.
    valid=valid&&ReadbackOut.Load(Trailer())<Width*Height/2&&ReadbackOut.Load(Trailer()+4)<Width*Height/8;
    float2 oldUv=uv;
    [loop]for(uint step=0;step<FaceAge&&valid;++step) {
        float4 guide=step==0?current:History(step-1,oldUv);
        oldUv+=guide.xy;
        valid=Inside(oldUv)&&Corresponds(guide,History(step,oldUv));
    }
    if(valid) {
        [loop]for(uint f=0;f<FaceCount;++f)mask=max(mask,FaceWeight(oldUv,Faces[f]));
    }
    FaceOut[id.xy]=float4(mask,current.z,max(current.w,0),valid?1:0);
    if(mask>.5){uint unused;ReadbackOut.InterlockedAdd(Trailer()+8,1,unused);}
}
)hlsl";
