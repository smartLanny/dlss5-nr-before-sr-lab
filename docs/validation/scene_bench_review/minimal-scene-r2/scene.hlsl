// Private finite 3D bench scene. Analytic opaque geometry; diffuse transport only.
// The camera and geometry are stationary, so physical current->previous MV is 0.
// Pixel sample displacement s and NGX jitter j=-s are separate from MV.
cbuffer Frame : register(b0) {
    uint2 extent;
    uint frame;
    uint samples;
    float2 displacement;
    float2 projectionScale;
    float3 camera;
    float storageScale;
};
RWTexture2D<float4> color : register(u0);
RWTexture2D<float> depth : register(u1);
RWTexture2D<float2> motion : register(u2);
RWTexture2D<float4> normal : register(u3);
RWTexture2D<float4> diffuseAlbedo : register(u4);
RWTexture2D<float4> specularAlbedo : register(u5);
RWTexture2D<float> roughness : register(u6);

struct Hit { float t; float3 n; uint material; };
uint Mix(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
    x *= 0x846ca68bu; return x ^ (x >> 16); }
float Random(inout uint seed) { seed=Mix(seed); return (seed >> 8) * (1.0 / 16777216.0); }

// Slabs explicitly handle rays parallel to a face. This also handles an origin
// inside the room: the nearest positive exit is a real room surface.
bool Box(float3 ro, float3 rd, float3 lo, float3 hi, out float t, out float3 n) {
    float nearT=-1e30, farT=1e30; float3 nearN=0,farN=0;
    [unroll] for(uint k=0;k<3;++k) {
        if(abs(rd[k]) < 1e-8) { if(ro[k]<lo[k] || ro[k]>hi[k]) {t=0;n=0;return false;} }
        else {
            float a=(lo[k]-ro[k])/rd[k],b=(hi[k]-ro[k])/rd[k];
            float3 an=0,bn=0;an[k]=-1;bn[k]=1;
            if(a>b) {float q=a;a=b;b=q;float3 qn=an;an=bn;bn=qn;}
            if(a>nearT){nearT=a;nearN=an;} if(b<farT){farT=b;farN=bn;}
        }
    }
    bool inside=nearT<=1e-4; t=inside?farT:nearT; n=inside?farN:nearN;
    return farT>=nearT && t>1e-4;
}
Hit Trace(float3 ro,float3 rd) {
    Hit h;h.t=1e30;h.n=0;h.material=0;
    float t;float3 n;
    if(Box(ro,rd,float3(-2,0,-5),float3(2,3,5),t,n)) {
        h.t=t;h.n=-n;h.material=1;
        float3 p=ro+t*rd;
        if(p.x < -1.999)h.material=2;
        if(p.x >  1.999)h.material=3;
        if(p.y > 2.999 && abs(p.x)<.7 && p.z>1.7 && p.z<3.1)h.material=4;
    }
    if(Box(ro,rd,float3(-1.35,0,1.4),float3(-.2,1.05,2.5),t,n) && t<h.t){h.t=t;h.n=n;h.material=5;}
    if(Box(ro,rd,float3(.35,0,2.55),float3(1.25,1.9,3.5),t,n) && t<h.t){h.t=t;h.n=n;h.material=6;}
    // Thin actual geometry gives a bounded visibility/aliasing case.
    if(Box(ro,rd,float3(-.08,0,1),float3(.0,1.55,1.08),t,n) && t<h.t){h.t=t;h.n=n;h.material=7;}
    return h;
}
float3 Albedo(Hit h,float3 p) {
    if(h.material==2)return float3(.65,.08,.06);
    if(h.material==3)return float3(.08,.55,.12);
    if(h.material==5)return float3(.7,.65,.5);
    if(h.material==6)return float3(.2,.35,.7);
    if(h.material==7)return .18;
    if(h.material==4)return 0;
    if(p.y<.001) {
        int check=(int(floor(p.x*5))+int(floor(p.z*5)))&1;
        return check?.58:.24;
    }
    return .65;
}
float3 Illuminate(Hit h,float3 p,float3 a,inout uint seed) {
    if(h.material==4)return 12;
    float3 lamp=float3((Random(seed)-.5)*1.4,3,1.7+Random(seed)*1.4);
    float3 to=lamp-p;float d2=dot(to,to),distance=sqrt(d2);float3 wi=to/distance;
    float c=max(0,dot(h.n,wi)),lc=max(0,wi.y);
    float3 shadowOrigin=p+h.n*1e-3;
    float3 shadowRay=lamp-shadowOrigin;float shadowDistance=length(shadowRay);
    Hit blocker=Trace(shadowOrigin,shadowRay/shadowDistance);
    float visible=blocker.t >= shadowDistance-2e-3;
    // One area-light sample; ambient is explicitly a constant diffuse term.
    return a*(.035 + visible*(12*1.96/3.14159265359)*c*lc/max(d2,1e-6));
}
[numthreads(8,8,1)]
void Produce(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=extent))return;
    float2 ndc=(float2(id.xy)+.5+displacement)/float2(extent)*2-1;
    float3 rd=normalize(float3(ndc.x/projectionScale.x,-ndc.y/projectionScale.y,1));
    Hit h=Trace(camera,rd);float3 p=camera+rd*h.t;float3 a=Albedo(h,p);
    uint seed=Mix(id.x+extent.x*id.y)^Mix(frame+0x9e3779b9u);
    float3 radiance=0;
    for(uint i=0;i<samples;++i)radiance+=Illuminate(h,p,a,seed);
    color[id.xy]=float4(radiance*(storageScale/samples),1);
    depth[id.xy]=.1/(p.z-camera.z); // P0: reversed infinite LH, near=.1
    motion[id.xy]=0;
    normal[id.xy]=float4(h.n,0); // Unpacked signed world normal, separate roughness.
    diffuseAlbedo[id.xy]=float4(a,1);
    specularAlbedo[id.xy]=float4(0,0,0,1); // Actual material has no specular lobe.
    roughness[id.xy]=1;
}
