#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef NR_FACE_BACKEND
#define NR_FACE_BACKEND 1
#endif
#if NR_FACE_BACKEND
#include <windows.h>
#include <bcrypt.h>
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmacro-redefined"
#endif
#include <onnxruntime_c_api.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include "nr_face_detector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace nr_face {
namespace {
constexpr char kRuntimeHash[] = "dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea";
constexpr char kModelHash[] = "b1b3061b1ae7a43bcc1497284ade754f701db774ef3a19481edcaf27fbc425b1";
std::string Hash(const void* bytes, std::size_t size) {
    if (!bytes || !size || size > 32u*1024u*1024u) return {};
    BCRYPT_ALG_HANDLE algorithm{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) return {};
    unsigned char digest[32]{};
    BCRYPT_HASH_HANDLE hash{};
    auto status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if(!status)status=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<void*>(bytes)),static_cast<ULONG>(size),0);
    if(!status)status=BCryptFinishHash(hash,digest,32,0);
    if(hash)BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status) return {};
    std::string hex(64, '0');
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned i=0;i<32;++i) { hex[2*i]=digits[digest[i]>>4];hex[2*i+1]=digits[digest[i]&15]; }
    return hex;
}
float IoU(const Face& a, const Face& b) {
    const float w=std::max(0.f,std::min(a.x+a.width,b.x+b.width)-std::max(a.x,b.x));
    const float h=std::max(0.f,std::min(a.y+a.height,b.y+b.height)-std::max(a.y,b.y));
    return w*h/std::max(1e-12f,a.width*a.height+b.width*b.height-w*h);
}
constexpr const char* kOutputs[] = {"cls_8","cls_16","cls_32","obj_8","obj_16","obj_32",
    "bbox_8","bbox_16","bbox_32","kps_8","kps_16","kps_32"};
}
struct Detector::Impl {
    HMODULE module{};
    const OrtApi* api{};
    OrtEnv* env{};
    OrtSession* session{};
    OrtMemoryInfo* memory{};
    std::vector<float> input;
    ~Impl() {
        if(memory)api->ReleaseMemoryInfo(memory);
        if(session)api->ReleaseSession(session);
        if(env)api->ReleaseEnv(env);
        if(module)FreeLibrary(module);
    }
    void Check(OrtStatus* status) {
        if(!status)return;
        std::string message=api->GetErrorMessage(status);api->ReleaseStatus(status);
        throw std::runtime_error(message);
    }
};
Detector::Detector() = default;
Detector::~Detector() = default;
bool Detector::Open(const wchar_t* path,const void* model,std::size_t size,const char* hash,std::string& error) {
    impl_.reset();error.clear();
    try {
        if(!path||!std::filesystem::path(path).is_absolute()||!hash||std::strcmp(hash,kModelHash)||Hash(model,size)!=kModelHash)
            throw std::runtime_error("Face model identity or absolute runtime path is invalid");
        auto p=std::make_unique<Impl>();
        // Hold a non-write-shared handle through hash + load, preventing replacement between them.
        HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Face CPU runtime is unavailable");
        LARGE_INTEGER length{};DWORD got{};
        bool valid=GetFileSizeEx(file,&length)&&length.QuadPart==14186016;
        std::vector<unsigned char> data(valid?static_cast<std::size_t>(length.QuadPart):0);
        valid=valid&&ReadFile(file,data.data(),static_cast<DWORD>(data.size()),&got,nullptr)&&got==data.size();
        valid=valid&&Hash(data.data(),data.size())==kRuntimeHash;
        if(valid)p->module=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        CloseHandle(file);
        if(!p->module)throw std::runtime_error("Face CPU runtime identity/load failed");
        const auto get=reinterpret_cast<decltype(&OrtGetApiBase)>(GetProcAddress(p->module,"OrtGetApiBase"));
        if(!get||std::strcmp(get()->GetVersionString(),"1.23.2")||!(p->api=get()->GetApi(ORT_API_VERSION)))
            throw std::runtime_error("Unsupported face CPU runtime ABI");
        p->Check(p->api->CreateEnv(ORT_LOGGING_LEVEL_ERROR,"nr-face",&p->env));
        OrtSessionOptions* options{};
        p->Check(p->api->CreateSessionOptions(&options));
        try {
            p->Check(p->api->SetIntraOpNumThreads(options,1));
            p->Check(p->api->SetInterOpNumThreads(options,1));
            p->Check(p->api->SetSessionExecutionMode(options,ORT_SEQUENTIAL));
            p->Check(p->api->AddSessionConfigEntry(options,"session.intra_op.allow_spinning","0"));
            p->Check(p->api->AddSessionConfigEntry(options,"session.inter_op.allow_spinning","0"));
            p->Check(p->api->SetSessionGraphOptimizationLevel(options,ORT_ENABLE_ALL));
            p->Check(p->api->CreateSessionFromArray(p->env,model,size,options,&p->session));
        } catch(...) {p->api->ReleaseSessionOptions(options);throw;}
        p->api->ReleaseSessionOptions(options);
        p->Check(p->api->CreateCpuMemoryInfo(OrtArenaAllocator,OrtMemTypeDefault,&p->memory));
        impl_=std::move(p);return true;
    } catch(const std::exception& e){error=e.what();return false;}
}
bool Detector::Detect(const std::uint8_t* bgr,unsigned width,unsigned height,std::size_t stride,
                      std::vector<Face>& faces,std::string& error) {
    faces.clear();error.clear();
    if(!impl_||!bgr||width<32||height<32||width>512||height>512||width%32||height%32||stride<width*3u) {
        error="Face reference must be BGR8, 32-aligned, at most 512x512, with an open detector";return false;
    }
    auto& p=*impl_;OrtValue* tensor{};std::array<OrtValue*,12> outputs{};
    auto release=[&]{for(auto* value:outputs)if(value)p.api->ReleaseValue(value);if(tensor)p.api->ReleaseValue(tensor);};
    try {
        const std::size_t pixels=static_cast<std::size_t>(width)*height;p.input.resize(pixels*3);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned c=0;c<3;++c)
            p.input[c*pixels+y*width+x]=bgr[y*stride+x*3+c];
        const std::int64_t shape[]={1,3,height,width};
        p.Check(p.api->CreateTensorWithDataAsOrtValue(p.memory,p.input.data(),p.input.size()*sizeof(float),
            shape,4,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&tensor));
        const char* name="input";
        p.Check(p.api->Run(p.session,nullptr,&name,&tensor,1,kOutputs,12,outputs.data()));
        std::array<const float*,12> data{};
        for(unsigned i=0;i<12;++i) {
            OrtTensorTypeAndShapeInfo* info{};p.Check(p.api->GetTensorTypeAndShape(outputs[i],&info));
            std::size_t count{};ONNXTensorElementDataType type{};
            OrtStatus* a=p.api->GetTensorShapeElementCount(info,&count);
            OrtStatus* b=p.api->GetTensorElementType(info,&type);p.api->ReleaseTensorTypeAndShapeInfo(info);
            if(a){if(b)p.api->ReleaseStatus(b);p.Check(a);}p.Check(b);
            const unsigned s=8u<<(i%3),channels=i/3==2?4u:i/3==3?10u:1u;
            if(type!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||count!=(width/s)*(height/s)*channels)
                throw std::runtime_error("Face output tensor contract mismatch");
            void* raw{};p.Check(p.api->GetTensorMutableData(outputs[i],&raw));data[i]=static_cast<const float*>(raw);
        }
        std::vector<Face> candidates;
        for(unsigned level=0;level<3;++level) {
            const unsigned s=8u<<level,cols=width/s,rows=height/s;
            for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<cols;++x) {
                const unsigned i=y*cols+x;
                const float cls=data[level][i],obj=data[level+3][i];
                if(!std::isfinite(cls)||!std::isfinite(obj))continue;
                const float score=std::sqrt(std::clamp(cls,0.f,1.f)*std::clamp(obj,0.f,1.f));
                if(score<0.6f)continue;
                const float* box=data[level+6]+i*4;const float* kps=data[level+9]+i*10;
                bool finite=true;for(unsigned k=0;k<4;++k)finite&=std::isfinite(box[k]);
                for(unsigned k=0;k<10;++k)finite&=std::isfinite(kps[k]);
                if(!finite||std::abs(box[2])>10||std::abs(box[3])>10)continue;
                Face f{};f.width=std::exp(box[2])*s/width;f.height=std::exp(box[3])*s/height;
                f.x=(x+box[0])*s/width-f.width*.5f;f.y=(y+box[1])*s/height-f.height*.5f;f.confidence=score;
                if(f.width<=0||f.height<=0||f.width>2||f.height>2||f.x>=1||f.y>=1||f.x+f.width<=0||f.y+f.height<=0)continue;
                for(unsigned k=0;k<5;++k){f.landmarks[k*2]=(x+kps[k*2])*s/width;f.landmarks[k*2+1]=(y+kps[k*2+1])*s/height;}
                candidates.push_back(f);
            }
        }
        std::stable_sort(candidates.begin(),candidates.end(),[](const Face&a,const Face&b){return a.confidence>b.confidence;});
        for(const auto& f:candidates) {
            bool suppressed=false;for(const auto& accepted:faces)if(IoU(f,accepted)>.3f){suppressed=true;break;}
            if(!suppressed)faces.push_back(f);
            if(faces.size()==16)break;
        }
        release();return true;
    } catch(const std::exception& e){release();faces.clear();error=e.what();return false;}
}
}
#else
#include "nr_face_detector.h"
namespace nr_face {
struct Detector::Impl {};
Detector::Detector()=default;
Detector::~Detector()=default;
bool Detector::Open(const wchar_t*,const void*,std::size_t,const char*,std::string& error) {
    error="Face detector was not included in this build";return false;
}
bool Detector::Detect(const std::uint8_t*,unsigned,unsigned,std::size_t,std::vector<Face>& faces,std::string& error) {
    faces.clear();error="Face detector was not included in this build";return false;
}
}
#endif
