// Private standalone host instrumentation; never included in the product DLL.
// C-interface D3D12, matching the existing complete-Core test host.
#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace scene_measurements {
struct Frame {
    unsigned id=0;
    UINT64 begin=0,end=0,fence=0,completed=0;
    double milliseconds=0,serial_wall_ms=0;
};
struct Memory {
    std::string phase;
    unsigned frame=0;
    LONGLONG counter=0;
    UINT64 usage=0,budget=0,reservation=0;
};
class Recorder {
    ID3D12QueryHeap* query_=nullptr;
    ID3D12Resource* readback_=nullptr;
    IDXGIAdapter3* adapter_=nullptr;
    UINT64 frequency_=0;
    LONGLONG wall_frequency_=0,wall_begin_=0;
    bool initialized_=false,enabled_=false,recording_=false,ended_=false,valid_=true;
    unsigned current_=0;
    bool Fail(){valid_=false;return false;}
public:
    std::vector<Frame> frames;
    std::vector<Memory> memory;
    Recorder()=default;
    Recorder(const Recorder&)=delete;
    Recorder& operator=(const Recorder&)=delete;
    // Caller drops only after its existing exact final fence+Reset proof.
    // Fatal opaque errors in the host use its existing _Exit quarantine path.
    void Drop(){
        if(readback_){ID3D12Resource_Release(readback_);readback_=nullptr;}
        if(query_){ID3D12QueryHeap_Release(query_);query_=nullptr;}
        if(adapter_){IDXGIAdapter3_Release(adapter_);adapter_=nullptr;}
    }
    bool Initialize(ID3D12Device* device,ID3D12CommandQueue* queue,IDXGIAdapter* adapter,bool enabled){
        if(initialized_)return Fail(); initialized_=true;
        if(!enabled)return true; // No device/queue/adapter method or allocation.
        if(!device||!queue||!adapter)return Fail();
        enabled_=true;
        LARGE_INTEGER wallFrequency{};
        if(!QueryPerformanceFrequency(&wallFrequency)||wallFrequency.QuadPart<=0)return Fail();
        wall_frequency_=wallFrequency.QuadPart;
        if(FAILED(ID3D12CommandQueue_GetTimestampFrequency(queue,&frequency_))||!frequency_)return Fail();
        if(FAILED(IDXGIAdapter_QueryInterface(adapter,IID_IDXGIAdapter3,(void**)&adapter_)))return Fail();
        D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=2;
        if(FAILED(ID3D12Device_CreateQueryHeap(device,&q,IID_ID3D12QueryHeap,(void**)&query_)))return Fail();
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width=2*sizeof(UINT64);d.Height=1;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
        d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_READBACK;
        if(FAILED(ID3D12Device_CreateCommittedResource(device,&h,D3D12_HEAP_FLAG_NONE,&d,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&readback_)))return Fail();
        return true;
    }
    bool Begin(ID3D12GraphicsCommandList* list,unsigned frame){
        if(!enabled_)return true;
        if(!valid_||!list||recording_||ended_||frame!=frames.size())return Fail();
        LARGE_INTEGER wallBegin{};if(!QueryPerformanceCounter(&wallBegin))return Fail();
        wall_begin_=wallBegin.QuadPart;
        current_=frame;recording_=true;
        ID3D12GraphicsCommandList_EndQuery(list,query_,D3D12_QUERY_TYPE_TIMESTAMP,0);
        return true;
    }
    bool End(ID3D12GraphicsCommandList* list){
        if(!enabled_)return true;
        if(!valid_||!list||!recording_||ended_)return Fail();
        ID3D12GraphicsCommandList_EndQuery(list,query_,D3D12_QUERY_TYPE_TIMESTAMP,1);
        ID3D12GraphicsCommandList_ResolveQueryData(list,query_,D3D12_QUERY_TYPE_TIMESTAMP,0,2,readback_,0);
        ended_=true;return true;
    }
    // Call after the host's successful Wait + recording Reset, while its actual
    // retained completion fence still exists. This adds no wait/signal/queue.
    bool Consume(ID3D12Fence* actualFence,UINT64 expectedValue,bool recordingResetProven){
        if(!enabled_)return true;
        if(!valid_||!recording_||!ended_||!actualFence||!expectedValue||!recordingResetProven)return Fail();
        UINT64 completed=ID3D12Fence_GetCompletedValue(actualFence);
        if(completed==std::numeric_limits<UINT64>::max()||completed<expectedValue)return Fail();
        D3D12_RANGE read{0,2*sizeof(UINT64)};void* mapped=nullptr;
        if(FAILED(ID3D12Resource_Map(readback_,0,&read,&mapped))||!mapped)return Fail();
        UINT64 ticks[2]{};std::memcpy(ticks,mapped,sizeof(ticks));D3D12_RANGE none{0,0};
        ID3D12Resource_Unmap(readback_,0,&none);
        if(ticks[1]<ticks[0])return Fail();
        double ms=double(ticks[1]-ticks[0])*1000.0/double(frequency_);
        if(!std::isfinite(ms))return Fail();
        LARGE_INTEGER wallEnd{};
        if(!QueryPerformanceCounter(&wallEnd)||wallEnd.QuadPart<wall_begin_)return Fail();
        double serialWall=double(wallEnd.QuadPart-wall_begin_)*1000.0/double(wall_frequency_);
        if(!std::isfinite(serialWall))return Fail();
        frames.push_back({current_,ticks[0],ticks[1],expectedValue,completed,ms,serialWall});
        recording_=ended_=false;return true;
    }
    bool Snapshot(const char* phase,unsigned frame=0){
        if(!enabled_)return true;
        if(!valid_||!phase||!adapter_)return Fail();
        // Static phase strings are deliberately restricted to JSON-safe ASCII.
        for(const char* p=phase;*p;++p)if(!((*p>='a'&&*p<='z')||*p=='_'))return Fail();
        DXGI_QUERY_VIDEO_MEMORY_INFO m{};LARGE_INTEGER now{};
        if(FAILED(IDXGIAdapter3_QueryVideoMemoryInfo(adapter_,0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&m))||
           !QueryPerformanceCounter(&now))return Fail();
        memory.push_back({phase,frame,now.QuadPart,m.CurrentUsage,m.Budget,m.CurrentReservation});
        return true;
    }
    bool Complete(unsigned total)const{return initialized_&&enabled_&&valid_&&!recording_&&!ended_&&frames.size()==total;}
    UINT64 Frequency()const{return frequency_;}
    // Append JSON object fields to an already opened result object. The caller
    // writes identity, exact route/model adoption and capture information.
    bool WriteFields(FILE* file,unsigned total)const{
        if(!file||!Complete(total))return false;
        bool ok=std::fprintf(file,"\"timing\":{\"scope\":\"scene+Core+nativeRR graphics GPU time; excludes present and image capture\",\"frequency_hz\":%llu,\"frames\":[",(unsigned long long)frequency_)>0;
        for(size_t i=0;i<frames.size()&&ok;++i){const auto& f=frames[i];
            ok=std::fprintf(file,"%s{\"id\":%u,\"begin\":%llu,\"end\":%llu,\"fence\":%llu,\"completed\":%llu,\"milliseconds\":%.17g,\"serial_wall_ms\":%.17g}",i?",":"",f.id,(unsigned long long)f.begin,(unsigned long long)f.end,(unsigned long long)f.fence,(unsigned long long)f.completed,f.milliseconds,f.serial_wall_ms)>0;}
        if(ok)ok=std::fprintf(file,"]},\"memory_samples\":[")>0;
        for(size_t i=0;i<memory.size()&&ok;++i){const auto& m=memory[i];
            ok=std::fprintf(file,"%s{\"phase\":\"%s\",\"frame\":%u,\"counter\":%lld,\"usage_bytes\":%llu,\"budget_bytes\":%llu,\"reservation_bytes\":%llu}",i?",":"",m.phase.c_str(),m.frame,(long long)m.counter,(unsigned long long)m.usage,(unsigned long long)m.budget,(unsigned long long)m.reservation)>0;}
        return ok&&std::fprintf(file,"]")>0;
    }
};
}
