// Real signed Feature18 on an owned D3D12 queue. Synthetic inputs, not a game test.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef NR_HOST_BACKEND
#include "src/nvfp4/backend.h"
#endif
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
#ifdef NR_HOST_BACKEND
struct Observation {
    fs::path directory;std::mutex mutex;unsigned launches=0;
    ComPtr<ID3D12QueryHeap> query;ComPtr<ID3D12Resource> readback;UINT count=0;UINT64 frequency=0;
    static constexpr UINT OriginalCapacity=4096;
    struct OriginalPoint {
        uintptr_t function=0,module=0;
        uint32_t launch_count=0,params_bytes=0,shared_bytes=0,grid[3]{},block[3]{};
        std::array<char,1024> name{};
        std::array<uint8_t,32> hash{};
        bool has_hash=false,identity_live=false,ffn=false,name_truncated=false,ended=false;
        int32_t status=0;
    };
    bool original_profile=false,original_overflow=false,original_invalid=false;
    bool original_range=false,range_began=false,range_ended=false;
    bool original_stages=false,stage_identity_mismatch=false,stage_identity_pending=false;
    std::array<bool,5> stage_seen{};UINT stage_boundary_count=0;
    uint64_t range_first=0,range_last=0,range_calls=0;
    UINT original_count=0;
    uint64_t original_pending=UINT64_MAX;
    std::vector<OriginalPoint> original_points;
    ComPtr<ID3D12QueryHeap> original_query;ComPtr<ID3D12Resource> original_readback;
    struct Point{nr::nvfp4::Stage stage;bool begin;unsigned block;};std::array<Point,128> points{};
    static void Stage(nr::nvfp4::Stage stage,bool begin,uint32_t block,ID3D12GraphicsCommandList*list,void*user){auto&self=*static_cast<Observation*>(user);
        // The full-call diagnostic keeps only disjoint replacement spans.
        // Original FFN, pack, expansion and contraction ranges would overlap.
        if(self.original_profile && stage!=nr::nvfp4::Stage::ReplacementPair)return;
        if(!self.query||self.count==self.points.size())return;
        self.points[self.count]={stage,begin,block};list->EndQuery(self.query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,self.count++);}
    std::array<double,6> Times(){std::array<double,6> result{};if(!count)return result;void*data=nullptr;D3D12_RANGE range{0,SIZE_T(count)*8};if(FAILED(readback->Map(0,&range,&data)))throw std::runtime_error("stage timestamp map");
        auto*t=static_cast<uint64_t*>(data);for(UINT i=0;i<count;++i){if(!points[i].begin)continue;for(UINT j=i+1;j<count;++j){if(points[i].stage==points[j].stage&&points[i].block==points[j].block&&!points[j].begin){if(t[j]<t[i])throw std::runtime_error("nonmonotonic stage timestamps");result[unsigned(points[i].stage)]+=double(t[j]-t[i])*1000.0/frequency;break;}}}
        D3D12_RANGE written{0,0};readback->Unmap(0,&written);return result;}
    static void Module(const void*data,uint32_t bytes,const uint8_t hash[32],void*user){auto&self=*static_cast<Observation*>(user);std::lock_guard lock(self.mutex);char name[65]{};
        for(unsigned i=0;i<32;++i)snprintf(name+i*2,3,"%02x",hash[i]);std::ofstream out(self.directory/(std::string(name)+".cubin"),std::ios::binary);out.write(static_cast<const char*>(data),bytes);}
    static void Launch(const char*name,const void*params,uint32_t bytes,const uint32_t grid[3],const uint32_t block[3],uint32_t shared,void*user){auto&self=*static_cast<Observation*>(user);std::lock_guard lock(self.mutex);if(self.launches>=16)return;auto index=self.launches++;
        std::ofstream raw(self.directory/("launch-"+std::to_string(index)+".bin"),std::ios::binary);raw.write(static_cast<const char*>(params),bytes);
        std::ofstream out(self.directory/"launches.jsonl",std::ios::app);out<<"{\"index\":"<<index<<",\"function\":\""<<name<<"\",\"params_bytes\":"<<bytes<<",\"grid\":["<<grid[0]<<','<<grid[1]<<','<<grid[2]<<"],\"block\":["<<block[0]<<','<<block[1]<<','<<block[2]<<"],\"shared\":"<<shared<<"}\n";}
    void ResetOriginal() noexcept {
        original_count=0;original_pending=UINT64_MAX;original_overflow=original_invalid=false;
        range_began=range_ended=false;range_calls=0;
        stage_seen.fill(false);stage_boundary_count=0;stage_identity_mismatch=stage_identity_pending=false;
    }
    static void CaptureOriginal(const nr::nvfp4::OriginalLaunchInfo& info,OriginalPoint& point) noexcept {
            point.function=info.function;point.module=info.module;point.launch_count=info.launch_count;
            point.params_bytes=info.params_bytes;point.shared_bytes=info.shared_bytes;
            std::copy(std::begin(info.grid),std::end(info.grid),point.grid);
            std::copy(std::begin(info.block),std::end(info.block),point.block);
            point.name[0]=0;point.name_truncated=false;
            if(info.function_name) {
                size_t n=0;while(n+1<point.name.size() && info.function_name[n]){point.name[n]=info.function_name[n];++n;}
                point.name[n]=0;point.name_truncated=info.function_name[n]!=0;
            }
            point.has_hash=info.module_sha256!=nullptr;
            if(point.has_hash)std::copy(info.module_sha256,info.module_sha256+32,point.hash.begin());
            point.identity_live=info.identity_live;point.ffn=info.ffn_target;point.ended=false;point.status=0;
    }
    static void Original(const nr::nvfp4::OriginalLaunchInfo& info,bool begin,int32_t status,
                         ID3D12GraphicsCommandList* list,void* user) noexcept {
        auto& self=*static_cast<Observation*>(user);
        if(!self.original_query) {self.original_overflow=true;return;}
        if(self.original_stages) {
            if(begin) {
                if(info.sequence!=self.range_calls || self.original_pending!=UINT64_MAX)self.original_invalid=true;
                ++self.range_calls;self.original_pending=info.sequence;
            } else {
                if(self.original_pending!=info.sequence || status)self.original_invalid=true;
                self.original_pending=UINT64_MAX;
            }
            struct Endpoint{uint64_t sequence;bool begin;const char* name;const char* hash;};
            static constexpr Endpoint endpoints[]={
                {0,true,"cc_cb_clear","661970ae2e3970b2d1541255c96ffabe241b4d3c5c2a59a895be1fde5db0dbe8"},
                {57,true,"cc_vit_1d_repack_2d_to_1d_fp8","3fa6f076eecfbb6e19c378f84bbb7025cd805ed4de9a2f9c9b995eb34a7f3d76"},
                {99,true,"cc_dec_input_upsample_1024_512_tilesync_fp8","661970ae2e3970b2d1541255c96ffabe241b4d3c5c2a59a895be1fde5db0dbe8"},
                {154,true,"cc_tinlayout_fused_post_block_swin_1h_32_fp8","c11e2eb6f6eeabadffe340dba74ec8c3f474f4ac4a8ed601d59e04d1d49bda3d"},
                {157,false,"cuda_font_kernel","779c283266c3ee724d2942b19465e702ebb4c8d057e3664cef070b3e1c81f056"}
            };
            for(UINT i=0;i<5;++i) {
                const auto& endpoint=endpoints[i];if(info.sequence!=endpoint.sequence||begin!=endpoint.begin)continue;
                const bool identity_ready=info.identity_live&&info.module_sha256&&info.function_name&&*info.function_name;
                bool matches=info.launch_count==1&&identity_ready&&
                    std::strcmp(info.function_name,endpoint.name)==0;
                if(matches)for(unsigned j=0;j<32;++j){auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
                    if(info.module_sha256[j]!=uint8_t(nibble(endpoint.hash[j*2])*16+nibble(endpoint.hash[j*2+1]))){matches=false;break;}}
                if(!matches){if(info.launch_count==1&&!identity_ready)self.stage_identity_pending=true;else self.stage_identity_mismatch=true;}
                if(self.stage_seen[i]||self.stage_boundary_count!=i)self.original_invalid=true;
                self.stage_seen[i]=true;++self.stage_boundary_count;CaptureOriginal(info,self.original_points[i]);
                list->EndQuery(self.original_query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,i);
            }
            return;
        }
        if(self.original_range) {
            if(begin) {
                if(info.sequence!=self.range_calls || self.original_pending!=UINT64_MAX)self.original_invalid=true;
                ++self.range_calls;self.original_pending=info.sequence;
                if(info.sequence==self.range_first) {
                    CaptureOriginal(info,self.original_points[0]);self.range_began=true;
                    list->EndQuery(self.original_query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
                }
            } else {
                if(self.original_pending!=info.sequence || status)self.original_invalid=true;
                self.original_pending=UINT64_MAX;
                if(info.sequence==self.range_last) {
                    CaptureOriginal(info,self.original_points[1]);self.range_ended=true;
                    list->EndQuery(self.original_query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
                }
            }
            return;
        }
        if(info.sequence>=self.original_points.size()) {self.original_overflow=true;return;}
        auto& point=self.original_points[size_t(info.sequence)];
        if(begin) {
            if(info.sequence!=self.original_count || self.original_pending!=UINT64_MAX)self.original_invalid=true;
            self.original_count=UINT(info.sequence)+1;self.original_pending=info.sequence;
            CaptureOriginal(info,point);
        } else {
            if(self.original_pending!=info.sequence)self.original_invalid=true;
            self.original_pending=UINT64_MAX;point.status=status;point.ended=true;
        }
        list->EndQuery(self.original_query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,UINT(info.sequence)*2+(begin?0:1));
    }
    static void JsonString(std::ostream& out,const char* text) {
        static constexpr char hex[]="0123456789abcdef";out<<'"';
        for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p) {
            if(*p=='"'||*p=='\\')out<<'\\'<<char(*p);
            else if(*p<0x20)out<<"\\u00"<<hex[*p>>4]<<hex[*p&15];else out<<char(*p);
        }
        out<<'"';
    }
    void WriteOriginalRange(std::ostream& summary,unsigned frame,unsigned submission,const char* phase,const uint64_t feature[2]) {
        if(original_overflow||original_invalid||original_pending!=UINT64_MAX||!range_began||!range_ended||range_calls<=range_last)
            throw std::runtime_error("incomplete original input range; endpoint missing or call failed");
        uint64_t ticks[2]{};void* data=nullptr;D3D12_RANGE range{0,sizeof(ticks)};
        if(FAILED(original_readback->Map(0,&range,&data)))throw std::runtime_error("original range timestamp map");
        std::memcpy(ticks,data,sizeof(ticks));D3D12_RANGE written{0,0};original_readback->Unmap(0,&written);
        if(ticks[0]<feature[0]||ticks[1]>feature[1]||ticks[1]<ticks[0])throw std::runtime_error("invalid original range timestamp bounds");
        const auto& first=original_points[0];const auto& last=original_points[1];
        const double total=double(feature[1]-feature[0])*1000.0/frequency,ms=double(ticks[1]-ticks[0])*1000.0/frequency;
        summary<<"{\"frame\":"<<frame<<",\"submission\":"<<submission<<",\"phase\":\""<<phase
               <<"\",\"gpu_complete\":true,\"complete\":true,\"profile_mode\":\"contiguous-input-range\""
               <<",\"sequence_basis\":\"incoming-original-nvapi-call-including-replaced\",\"range_first\":"<<range_first
               <<",\"range_last\":"<<range_last<<",\"observed_input_calls\":"<<range_calls
               <<",\"range_input_calls\":"<<range_last-range_first+1<<",\"range_timestamp_count\":2,\"frequency_hz\":"<<frequency
               <<",\"feature_begin_ticks\":"<<feature[0]<<",\"feature_end_ticks\":"<<feature[1]
               <<",\"gpu_begin_ticks\":"<<ticks[0]<<",\"gpu_end_ticks\":"<<ticks[1]
               <<",\"gpu_begin_ms\":"<<double(ticks[0]-feature[0])*1000.0/frequency
               <<",\"gpu_end_ms\":"<<double(ticks[1]-feature[0])*1000.0/frequency
               <<",\"feature18_ms\":"<<total<<",\"range_ms\":"<<ms<<",\"outside_range_ms\":"<<total-ms
               <<",\"first_function_name\":";JsonString(summary,first.name.data());
        summary<<",\"last_function_name\":";JsonString(summary,last.name.data());
        summary<<",\"first_function_handle\":\"0x"<<std::hex<<first.function<<"\",\"last_function_handle\":\"0x"<<last.function<<std::dec<<'"';
        for(const auto& endpoint:{std::make_pair("first",&first),std::make_pair("last",&last)}) {
            const auto& point=*endpoint.second;
            summary<<",\""<<endpoint.first<<"_module_sha256\":";
            if(point.has_hash){summary<<'"';static constexpr char hex[]="0123456789abcdef";for(auto byte:point.hash)summary<<hex[byte>>4]<<hex[byte&15];summary<<'"';}else summary<<"null";
            summary<<",\""<<endpoint.first<<"_identity_live\":"<<(point.identity_live?"true":"false")
                   <<",\""<<endpoint.first<<"_name_truncated\":"<<(point.name_truncated?"true":"false");
        }
        summary<<"}\n";if(!summary)throw std::runtime_error("original range profile output failed");
    }
    void WriteOriginalStages(std::ostream& summary,unsigned frame,unsigned submission,const char* phase,const uint64_t feature[2]) {
        if(original_overflow||original_invalid||original_pending!=UINT64_MAX||range_calls!=158||stage_boundary_count!=5||
           !std::all_of(stage_seen.begin(),stage_seen.end(),[](bool seen){return seen;}))
            throw std::runtime_error("fixed stages require exactly 158 ordered input calls and all five endpoints");
        if(stage_identity_mismatch)throw std::runtime_error("fixed stage endpoint function/module identity mismatch");
        if(stage_identity_pending) {
            // A function first created inside Evaluate is absent from that
            // scope's frozen identity snapshot. Record the incomplete warmup
            // explicitly; a measured sample may never use this exception.
            if(std::strcmp(phase,"warmup")&&std::strcmp(phase,"preparing"))
                throw std::runtime_error("measured fixed stage endpoint identity is not ready");
            summary<<"{\"frame\":"<<frame<<",\"submission\":"<<submission<<",\"phase\":\""<<phase
                   <<"\",\"gpu_complete\":true,\"complete\":false,\"valid_stage_sample\":false"
                   <<",\"reason\":\"endpoint_identity_pending\",\"profile_mode\":\"fixed-four-input-stages\""
                   <<",\"observed_input_calls\":"<<range_calls<<",\"stage_timestamp_count\":5,\"endpoint_information\":[";
            static constexpr uint64_t sequence[]={0,57,99,154,157};
            for(size_t i=0;i<5;++i) {
                if(i)summary<<',';const auto& point=original_points[i];
                summary<<"{\"input_sequence\":"<<sequence[i]<<",\"after_call\":"<<(i==4?"true":"false")
                       <<",\"function_handle\":\"0x"<<std::hex<<point.function<<"\",\"module_handle\":\"0x"<<point.module<<std::dec
                       <<"\",\"function_name\":";JsonString(summary,point.name.data());summary<<",\"module_sha256\":";
                if(point.has_hash){summary<<'"';static constexpr char hex[]="0123456789abcdef";for(auto byte:point.hash)summary<<hex[byte>>4]<<hex[byte&15];summary<<'"';}else summary<<"null";
                summary<<",\"identity_live\":"<<(point.identity_live?"true":"false")<<",\"name_truncated\":"<<(point.name_truncated?"true":"false")
                       <<",\"launch_count\":"<<point.launch_count<<'}';
            }
            summary<<"]}\n";if(!summary)throw std::runtime_error("pending stage profile output failed");return;
        }
        std::array<uint64_t,5> ticks{};void* data=nullptr;D3D12_RANGE range{0,sizeof(ticks)};
        if(FAILED(original_readback->Map(0,&range,&data)))throw std::runtime_error("original stage timestamp map");
        std::memcpy(ticks.data(),data,sizeof(ticks));D3D12_RANGE written{0,0};original_readback->Unmap(0,&written);
        if(ticks.front()<feature[0]||ticks.back()>feature[1]||!std::is_sorted(ticks.begin(),ticks.end()))
            throw std::runtime_error("fixed stage GPU timestamps are outside Feature18 or nonmonotonic");
        static constexpr const char* names[]={"front_swin","vit_1d","back_swin","post_tail"};
        uint64_t sum=0;for(size_t i=0;i<4;++i)sum+=ticks[i+1]-ticks[i];
        const uint64_t span=ticks.back()-ticks.front(),outside=(ticks.front()-feature[0])+(feature[1]-ticks.back());
        if(sum!=span||sum+outside!=feature[1]-feature[0])throw std::runtime_error("fixed stage accounting does not close");
        summary<<"{\"frame\":"<<frame<<",\"submission\":"<<submission<<",\"phase\":\""<<phase
               <<"\",\"gpu_complete\":true,\"complete\":true,\"profile_mode\":\"fixed-four-input-stages\""
               <<",\"sequence_basis\":\"incoming-original-nvapi-call-including-replaced\",\"observed_input_calls\":"<<range_calls
               <<",\"stage_timestamp_count\":5,\"boundaries_validated\":true,\"frequency_hz\":"<<frequency
               <<",\"boundary_input_sequences\":[0,57,99,154,157],\"last_boundary_after_call\":true"
               <<",\"feature_begin_ticks\":"<<feature[0]<<",\"feature_end_ticks\":"<<feature[1]<<",\"stage_ticks\":[";
        for(size_t i=0;i<ticks.size();++i){if(i)summary<<',';summary<<ticks[i];}
        summary<<"],\"stage_ms\":{";
        for(size_t i=0;i<4;++i){if(i)summary<<',';summary<<'"'<<names[i]<<"\":"<<double(ticks[i+1]-ticks[i])*1000.0/frequency;}
        summary<<"},\"original_call_region_ms\":"<<double(span)*1000.0/frequency
               <<",\"feature18_ms\":"<<double(feature[1]-feature[0])*1000.0/frequency
               <<",\"outside_original_call_region_ms\":"<<double(outside)*1000.0/frequency
               <<",\"closure_error_ticks\":0,\"endpoint_functions\":[";
        for(size_t i=0;i<5;++i){if(i)summary<<',';JsonString(summary,original_points[i].name.data());}
        summary<<"]}\n";if(!summary)throw std::runtime_error("original stage profile output failed");
    }
    void WriteOriginal(std::ostream& out,std::ostream& summary,unsigned frame,unsigned submission,
                       const char* phase,const uint64_t feature[2]) {
        if(original_stages){WriteOriginalStages(summary,frame,submission,phase,feature);return;}
        if(original_range){WriteOriginalRange(summary,frame,submission,phase,feature);return;}
        if(original_overflow || original_invalid || original_pending!=UINT64_MAX)
            throw std::runtime_error("original launch timestamp capacity/pairing failure; profile is incomplete");
        struct Interval{uint64_t begin,end;};std::vector<Interval> intervals;
        intervals.reserve(original_count+8);
        std::vector<uint64_t> ticks(size_t(original_count)*2);
        if(original_count) {
            void* data=nullptr;D3D12_RANGE range{0,ticks.size()*sizeof(uint64_t)};
            if(FAILED(original_readback->Map(0,&range,&data)))throw std::runtime_error("original timestamp map");
            std::memcpy(ticks.data(),data,range.End);D3D12_RANGE written{0,0};original_readback->Unmap(0,&written);
        }
        double known_ffn=0,known_other=0,unknown=0,replacement=0;unsigned unknown_calls=0;
        for(UINT i=0;i<original_count;++i) {
            const auto& p=original_points[i];const uint64_t begin=ticks[i*2],end=ticks[i*2+1];
            if(!p.ended || p.status || begin<feature[0] || end>feature[1] || end<begin)
                throw std::runtime_error("failed original launch or invalid GPU timestamp; profile is incomplete");
            const double ms=double(end-begin)*1000.0/frequency;
            const bool known=p.identity_live&&p.has_hash&&p.name[0]&&p.launch_count==1;
            const char* category=known?(p.ffn?"original_ffn":"original_other"):"original_unknown";
            if(!known){unknown+=ms;++unknown_calls;}else if(p.ffn)known_ffn+=ms;else known_other+=ms;
            intervals.push_back({begin,end});
            out<<"{\"frame\":"<<frame<<",\"submission\":"<<submission<<",\"phase\":\""<<phase
               <<"\",\"sequence\":"<<i<<",\"launch_count\":"<<p.launch_count
               <<",\"function_handle\":\"0x"<<std::hex<<p.function<<"\",\"module_handle\":\"0x"<<p.module<<std::dec
               <<"\",\"function_name\":";JsonString(out,p.name.data());out<<",\"module_sha256\":";
            if(p.has_hash){out<<'"';static constexpr char hex[]="0123456789abcdef";for(auto byte:p.hash)out<<hex[byte>>4]<<hex[byte&15];out<<'"';}else out<<"null";
            out<<",\"identity_live\":"<<(p.identity_live?"true":"false")<<",\"name_truncated\":"<<(p.name_truncated?"true":"false")
               <<",\"category\":\""<<category<<"\",\"nvapi_status\":"<<p.status
               <<",\"gpu_begin_ticks\":"<<begin<<",\"gpu_end_ticks\":"<<end
               <<",\"gpu_begin_ms\":"<<double(begin-feature[0])*1000.0/frequency
               <<",\"gpu_end_ms\":"<<double(end-feature[0])*1000.0/frequency<<",\"gpu_ms\":"<<ms
               <<",\"params_bytes\":"<<p.params_bytes<<",\"shared_bytes\":"<<p.shared_bytes
               <<",\"grid\":["<<p.grid[0]<<','<<p.grid[1]<<','<<p.grid[2]<<"],\"block\":["<<p.block[0]<<','<<p.block[1]<<','<<p.block[2]<<"]}\n";
        }
        if(count) {
            std::vector<uint64_t> stage_ticks(count);void* data=nullptr;D3D12_RANGE range{0,stage_ticks.size()*sizeof(uint64_t)};
            if(FAILED(readback->Map(0,&range,&data)))throw std::runtime_error("replacement timestamp map");
            std::memcpy(stage_ticks.data(),data,range.End);D3D12_RANGE written{0,0};readback->Unmap(0,&written);
            if(count%2)throw std::runtime_error("incomplete replacement timestamp pair");
            for(UINT i=0;i<count;i+=2) {
                if(points[i].stage!=nr::nvfp4::Stage::ReplacementPair || !points[i].begin || points[i+1].begin || points[i].block!=points[i+1].block ||
                   stage_ticks[i]<feature[0] || stage_ticks[i+1]>feature[1] || stage_ticks[i+1]<stage_ticks[i])
                    throw std::runtime_error("invalid replacement timestamp pair");
                intervals.push_back({stage_ticks[i],stage_ticks[i+1]});
                replacement+=double(stage_ticks[i+1]-stage_ticks[i])*1000.0/frequency;
            }
        }
        std::sort(intervals.begin(),intervals.end(),[](const Interval& a,const Interval& b){return a.begin<b.begin || (a.begin==b.begin&&a.end<b.end);});
        uint64_t cursor=feature[0],covered=0;
        for(const auto& interval:intervals) {
            if(interval.begin<cursor)throw std::runtime_error("overlapping GPU intervals cannot be added");
            covered+=interval.end-interval.begin;cursor=interval.end;
        }
        const double total=double(feature[1]-feature[0])*1000.0/frequency;
        const double remainder=double(feature[1]-feature[0]-covered)*1000.0/frequency;
        summary<<"{\"frame\":"<<frame<<",\"submission\":"<<submission<<",\"phase\":\""<<phase
               <<"\",\"gpu_complete\":true,\"complete\":true,\"frequency_hz\":"<<frequency
               <<",\"feature_begin_ticks\":"<<feature[0]<<",\"feature_end_ticks\":"<<feature[1]
               <<",\"feature18_ms\":"<<total<<",\"original_calls\":"<<original_count
               <<",\"original_unknown_calls\":"<<unknown_calls<<",\"original_ffn_ms\":"<<known_ffn
               <<",\"original_other_ms\":"<<known_other<<",\"original_unknown_ms\":"<<unknown
               <<",\"replacement_pairs_ms\":"<<replacement<<",\"unattributed_ms\":"<<remainder
               <<",\"accounting\":\"disjoint-original-calls-plus-replacement-pairs\"}\n";
        if(!out || !summary)throw std::runtime_error("original launch profile output failed");
    }
};
#endif
using Result = uint32_t;
struct Params { void** vtable; };
template<class T> T Entry(HMODULE m, const char* name) {
    auto p=GetProcAddress(m,name); if(!p) throw std::runtime_error(std::string("missing export: ")+name);
    return reinterpret_cast<T>(p);
}
template<class T> void Set(Params* p,unsigned slot,const char* name,T v) {
    reinterpret_cast<void(*)(Params*,const char*,T)>(p->vtable[slot])(p,name,v);
}
void UInt(Params*p,const char*k,unsigned v){Set(p,4,k,v);}
void Int(Params*p,const char*k,int v){Set(p,3,k,v);}
void Float(Params*p,const char*k,float v){Set(p,6,k,v);}
void Resource(Params*p,const char*k,ID3D12Resource*v){Set(p,1,k,v);}
void Check(HRESULT r,const char* what){if(FAILED(r))throw std::runtime_error(std::string(what)+" HRESULT="+std::to_string(uint32_t(r)));}
void Ngx(Result r,const char*what){std::cerr<<what<<"=0x"<<std::hex<<r<<std::dec<<'\n';if(r!=1&&r!=0xBAD00003u)throw std::runtime_error(what);}
[[noreturn]] void GpuFailure(const char*why){std::cerr<<"submitted GPU work uncertain: "<<why<<'\n';std::cerr.flush();ExitProcess(3);}
struct Device {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence; HANDLE event=nullptr;uint64_t serial=0;
    Device(){ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
        for(UINT i=0;;++i){ComPtr<IDXGIAdapter1>a;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a))==DXGI_ERROR_NOT_FOUND)break;
            DXGI_ADAPTER_DESC1 d{};a->GetDesc1(&d);if(d.VendorId!=0x10de||(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))continue;
            if(SUCCEEDED(D3D12CreateDevice(a.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)))){std::wcerr<<L"adapter="<<d.Description<<L'\n';break;}}
        if(!device)throw std::runtime_error("NVIDIA D3D12 device unavailable");D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");Check(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)),"allocator");
        Check(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("event");}
    ~Device(){if(event)CloseHandle(event);}
    void Submit(uint64_t token=0){Check(list->Close(),"close");ID3D12CommandList*l[]={list.Get()};queue->ExecuteCommandLists(1,l);
        const auto value=++serial;if(FAILED(queue->Signal(fence.Get(),value)))GpuFailure("signal");
#ifdef NR_HOST_BACKEND
        if(token)nr::nvfp4::OnSubmit(token,queue.Get(),fence.Get(),value);
#else
        (void)token;
#endif
        if(FAILED(fence->SetEventOnCompletion(value,event)))GpuFailure("event arm");
        if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0||fence->GetCompletedValue()==UINT64_MAX||fence->GetCompletedValue()<value)GpuFailure("timeout/device removed");
        Check(allocator->Reset(),"allocator reset");Check(list->Reset(allocator.Get(),nullptr),"list reset");
#ifdef NR_HOST_BACKEND
        if(token)nr::nvfp4::OnReset(token,false);
#endif
    }
};
ComPtr<ID3D12Resource> Buffer(Device&d,uint64_t bytes,D3D12_HEAP_TYPE kind){D3D12_HEAP_PROPERTIES h{};h.Type=kind;D3D12_RESOURCE_DESC r{};
    r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=bytes;r.Height=1;r.DepthOrArraySize=1;r.MipLevels=1;r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource>b;Check(d.device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&r,kind==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&b)),"buffer");return b;}
ComPtr<ID3D12Resource> Texture(Device&d,UINT w,UINT h,DXGI_FORMAT f){D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC r{};
    r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.Width=w;r.Height=h;r.DepthOrArraySize=1;r.MipLevels=1;r.Format=f;r.SampleDesc.Count=1;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource>out;Check(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&out)),"texture");return out;}
void Barrier(Device&d,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};d.list->ResourceBarrier(1,&x);}
uint16_t Half(float value){uint32_t x;memcpy(&x,&value,4);uint32_t sign=(x>>16)&0x8000;int e=int((x>>23)&255)-127+15;uint32_t m=x&0x7fffff;
    if(e<=0){if(e<-10)return uint16_t(sign);m|=0x800000;const unsigned shift=unsigned(14-e);const uint32_t out=m>>shift,rem=m&((1u<<shift)-1);return uint16_t(sign+out+(rem>(1u<<(shift-1))||(rem==(1u<<(shift-1))&&(out&1))));}
    if(e>=31)return uint16_t(sign|0x7c00);uint32_t out=(uint32_t(e)<<10)|(m>>13);out+=(m&8191)>4096||((m&8191)==4096&&(out&1));return uint16_t(sign|out);}
float Unhalf(uint16_t h){const float sign=(h&0x8000)?-1.f:1.f;const unsigned e=(h>>10)&31,m=h&1023;if(e==31)return m?NAN:sign*INFINITY;return sign*std::ldexp(float(e?1024+m:m),e?int(e)-25:-24);}
void Upload(Device&d,ID3D12Resource*r,unsigned channels,bool depth){auto desc=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT rows;UINT64 row,bytes;d.device->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&row,&bytes);
    auto upload=Buffer(d,bytes,D3D12_HEAP_TYPE_UPLOAD);void*m=nullptr;Check(upload->Map(0,nullptr,&m),"upload map");memset(m,0,size_t(bytes));
    for(UINT y=0;y<desc.Height;++y){auto*p=static_cast<uint8_t*>(m)+fp.Offset+uint64_t(y)*fp.Footprint.RowPitch;for(UINT x=0;x<desc.Width;++x){if(depth){float v=0.25f+0.5f*float(y)/desc.Height;memcpy(p+x*4,&v,4);}else for(unsigned c=0;c<channels;++c){float v=0.f;if(channels==4)v=c==3?1.f:(0.1f+0.6f*float((x/16+y/16+c*3)%17)/16.f);auto h=Half(v);memcpy(p+(x*channels+c)*2,&h,2);}}}
    upload->Unmap(0,nullptr);Barrier(d,r,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=r;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;d.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    Barrier(d,r,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);d.Submit();}
void Dump(Device&d,ID3D12Resource*r,const fs::path&path){const auto desc=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT rows;UINT64 row,bytes;d.device->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&row,&bytes);
    auto out=Buffer(d,bytes,D3D12_HEAP_TYPE_READBACK);Barrier(d,r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=r;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=out.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;d.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(d,r,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);d.Submit();
    void*m;D3D12_RANGE read{0,SIZE_T(bytes)};Check(out->Map(0,&read,&m),"readback map");std::ofstream f(path,std::ios::binary);if(!f)throw std::runtime_error("output file");size_t bad=0;double sum=0;
    for(UINT y=0;y<desc.Height;++y){auto*p=static_cast<uint8_t*>(m)+fp.Offset+uint64_t(y)*fp.Footprint.RowPitch;f.write(reinterpret_cast<char*>(p),std::streamsize(row));for(size_t x=0;x<row/2;++x){uint16_t h;memcpy(&h,p+x*2,2);float v=Unhalf(h);if(!std::isfinite(v))++bad;else sum+=v;}}
    D3D12_RANGE written{0,0};out->Unmap(0,&written);std::cerr<<"output_nonfinite="<<bad<<" sum="<<sum<<'\n';if(bad)throw std::runtime_error("nonfinite NR output");}
void Bind(Params*p,unsigned w,unsigned h,ID3D12Resource*c,ID3D12Resource*z,ID3D12Resource*mv,ID3D12Resource*out){
    Resource(p,"DLSSNR.Color",c);Resource(p,"DLSSNR.Output",out);Resource(p,"DLSSNR.Depth",z);Resource(p,"DLSSNR.MVec",mv);
    for(auto k:{"Width","OutWidth","DLSSNR.Width","DLSSNR.InputWidth","DLSSNR.OutputWidth","DLSSNR.Output.Width","DLSSNR.ColorSubrectWidth","DLSSNR.DepthSubrectWidth","DLSSNR.MVecSubrectWidth","DLSSNR.OutputSubrectWidth"})UInt(p,k,w);
    for(auto k:{"Height","OutHeight","DLSSNR.Height","DLSSNR.InputHeight","DLSSNR.OutputHeight","DLSSNR.Output.Height","DLSSNR.ColorSubrectHeight","DLSSNR.DepthSubrectHeight","DLSSNR.MVecSubrectHeight","DLSSNR.OutputSubrectHeight"})UInt(p,k,h);
    for(auto k:{"DLSSNR.ColorSubrectBaseX","DLSSNR.ColorSubrectBaseY","DLSSNR.DepthSubrectBaseX","DLSSNR.DepthSubrectBaseY","DLSSNR.MVecSubrectBaseX","DLSSNR.MVecSubrectBaseY","DLSSNR.OutputSubrectBaseX","DLSSNR.OutputSubrectBaseY"})UInt(p,k,0);
    UInt(p,"CreationNodeMask",1);UInt(p,"VisibilityNodeMask",1);Int(p,"PerfQualityValue",5);Int(p,"DLSS.Feature.Create.Flags",10);Int(p,"DLSSNR.DepthInverted",1);Int(p,"DLSSNR.Enabled",1);Int(p,"DLSSNR.Upscaling",0);
    Float(p,"DLSSNR.ScalingRatio",1);Float(p,"DLSSNR.Scale",1);Float(p,"DLSSNR.MV.Scale.X",1);Float(p,"DLSSNR.MV.Scale.Y",1);Float(p,"MV.Scale.X",1);Float(p,"MV.Scale.Y",1);
    Float(p,"DLSS.Pre.Exposure",1);Float(p,"DLSS.Exposure.Scale",1);Float(p,"DLSSNR.Intensity",1.2f);Float(p,"DLSSNR.LocalToneStrength",1);Float(p,"DLSSNR.LocalStructureStrength",1);Float(p,"DLSSNR.SkinStructureStrength",1);
    Int(p,"DLSSNR.UseAutoMask",0);UInt(p,"DLSSNR.Style",0);UInt(p,"DLSSNR.UICorrection",1);Int(p,"DLSSNR.Reset",1);
}
int wmain(int argc,wchar_t**argv){try{
    if(argc<7){std::cerr<<"nr_host <NR dll> <bridge dll> <asset dir> <output dir> <width> <height> [frames=16] [mode=fp8] [warmup=8]\n";return 1;}
    const fs::path runtime=fs::absolute(argv[1]),bridge=fs::absolute(argv[2]),assets=fs::absolute(argv[3]),output=fs::absolute(argv[4]);
    const unsigned w=std::stoul(argv[5]),h=std::stoul(argv[6]),frames=argc>7?std::stoul(argv[7]):16,warmup=argc>9?std::stoul(argv[9]):8;
    if(!w||!h||w>7680||h>4320||frames>100000||warmup>10000)throw std::runtime_error("invalid dimensions/frame count");
    const std::wstring mode=argc>8?argv[8]:L"fp8";
    if(mode!=L"fp8"&&mode!=L"passthrough"&&mode!=L"reference"&&mode!=L"optimized"&&mode!=L"fused"&&mode!=L"two")throw std::runtime_error("unknown host experiment mode");
    wchar_t profile_switch[2]{};const auto profile_length=GetEnvironmentVariableW(L"NR_HOST_PROFILE_LAUNCHES",profile_switch,2);
    if(profile_length && (profile_length!=1 || (profile_switch[0]!=L'0'&&profile_switch[0]!=L'1')))
        throw std::runtime_error("NR_HOST_PROFILE_LAUNCHES must be 0 or 1");
    wchar_t range_switch[64]{};const auto range_length=GetEnvironmentVariableW(L"NR_HOST_PROFILE_RANGE",range_switch,64);
    if(range_length>=64)throw std::runtime_error("NR_HOST_PROFILE_RANGE is too long");
    const bool profile_range=range_length!=0;uint64_t range_first=0,range_last=0;
    if(profile_range) {
        const std::wstring text(range_switch,range_length);const auto separator=text.find(L':');
        if(separator==std::wstring::npos||separator==0||separator+1==text.size()||
           text.find_first_not_of(L"0123456789",0)<separator||text.find_first_not_of(L"0123456789",separator+1)!=std::wstring::npos)
            throw std::runtime_error("NR_HOST_PROFILE_RANGE must be inclusive first:last decimal input-call indices");
        range_first=std::stoull(text.substr(0,separator));range_last=std::stoull(text.substr(separator+1));
        if(range_first>range_last||range_last>=4096)throw std::runtime_error("NR_HOST_PROFILE_RANGE must satisfy 0 <= first <= last < 4096");
    }
    wchar_t stages_switch[2]{};const auto stages_length=GetEnvironmentVariableW(L"NR_HOST_PROFILE_STAGES",stages_switch,2);
    if(stages_length && (stages_length!=1 || (stages_switch[0]!=L'0'&&stages_switch[0]!=L'1')))
        throw std::runtime_error("NR_HOST_PROFILE_STAGES must be 0 or 1");
    const bool profile_stages=stages_length==1&&stages_switch[0]==L'1';
    if(profile_stages&&profile_range)throw std::runtime_error("select fixed stages or an explicit range, not both");
    const bool profile_grouped=profile_range||profile_stages;
    const bool profile_original=(profile_length==1&&profile_switch[0]==L'1')||profile_grouped;
#ifndef NR_HOST_BACKEND
    if(profile_original)throw std::runtime_error("original launch profiling requires the observer-enabled host build");
#endif
    fs::create_directories(output);Device d;
#ifdef NR_HOST_BACKEND
    nr::nvfp4::Config cfg;cfg.asset_directory=assets.c_str();cfg.nr_runtime_path=runtime.c_str();cfg.mode=mode==L"fp8"?nr::nvfp4::Mode::Disabled:mode==L"passthrough"?nr::nvfp4::Mode::Passthrough:mode==L"reference"?nr::nvfp4::Mode::UpstreamReference:nr::nvfp4::Mode::Optimized;
    Observation observation;observation.directory=output;observation.original_profile=profile_original;
    observation.original_range=profile_range;observation.range_first=range_first;observation.range_last=range_last;
    observation.original_stages=profile_stages;
    cfg.observe_module=profile_original?nullptr:&Observation::Module;cfg.observe_launch=profile_original?nullptr:&Observation::Launch;
    cfg.observe_stage=profile_grouped?nullptr:&Observation::Stage;cfg.observer_user=&observation;
    if(profile_original){observation.original_points.resize(profile_stages?5:profile_range?2:Observation::OriginalCapacity);cfg.observe_original_launch=&Observation::Original;cfg.observe_original_input_calls=profile_grouped;}
    nr::nvfp4::OptimizedAcceptance acceptance;std::wstring optimized_dir;std::array<std::string,4> accepted_strings;std::vector<uint32_t> accepted_m;
    nr::nvfp4::ExpansionAcceptance expansion_acceptance;std::wstring expansion_dir;std::array<std::string,4> expansion_strings;
    if(mode==L"optimized"||mode==L"fused"||mode==L"two"){
        if(argc<11)throw std::runtime_error("optimized mode requires a privately verified acceptance text file");std::ifstream f{fs::path(argv[10])};if(!f)throw std::runtime_error("acceptance file missing");
        std::string line;std::getline(f,line);optimized_dir=fs::u8path(line).wstring();for(auto&s:accepted_strings)if(!std::getline(f,s))throw std::runtime_error("incomplete acceptance file");
        std::getline(f,line);acceptance.variant=unsigned(std::stoul(line));while(std::getline(f,line)){if(!line.empty())accepted_m.push_back(unsigned(std::stoul(line)));}if(accepted_m.empty())throw std::runtime_error("no accepted shapes");
        acceptance.directory=optimized_dir.c_str();acceptance.cubin_sha256=accepted_strings[0].c_str();acceptance.builder_sha256=accepted_strings[1].c_str();acceptance.build_id=accepted_strings[2].c_str();acceptance.kernel_id=accepted_strings[3].c_str();acceptance.tested_m=accepted_m.data();acceptance.tested_m_count=unsigned(accepted_m.size());cfg.optimized_acceptance=&acceptance;
        acceptance.fused_publish=mode==L"fused"||mode==L"two";
        if(mode==L"two"){
            if(argc<12)throw std::runtime_error("two-launch mode requires an explicit expansion receipt");
            std::ifstream ef{fs::path(argv[11])};if(!ef)throw std::runtime_error("expansion receipt missing");
            std::getline(ef,line);expansion_dir=fs::u8path(line).wstring();for(auto&s:expansion_strings)if(!std::getline(ef,s))throw std::runtime_error("incomplete expansion receipt");
            if(!std::getline(ef,line))throw std::runtime_error("missing expansion variant");
            expansion_acceptance.directory=expansion_dir.c_str();expansion_acceptance.cubin_sha256=expansion_strings[0].c_str();
            expansion_acceptance.builder_sha256=expansion_strings[1].c_str();expansion_acceptance.build_id=expansion_strings[2].c_str();
            expansion_acceptance.kernel_id=expansion_strings[3].c_str();expansion_acceptance.variant=unsigned(std::stoul(line));
            acceptance.expansion=&expansion_acceptance;
        }
    }
    if(!nr::nvfp4::Init(cfg)||(profile_original&&!nr::nvfp4::ArmObserver())||
       (cfg.mode!=nr::nvfp4::Mode::Disabled&&!nr::nvfp4::EnsureStarted(d.device.Get())))throw std::runtime_error("backend startup");
#else
    if(mode!=L"fp8")throw std::runtime_error("this host build has no backend");
#endif
    wchar_t driver[MAX_PATH]{};DWORD n=sizeof(driver);Check(HRESULT_FROM_WIN32(RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore",L"FullPath",RRF_RT_REG_SZ,nullptr,driver,&n)),"NGX driver path");
    const auto core=LoadLibraryExW((fs::path(driver)/L"_nvngx.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const auto nr=LoadLibraryExW(runtime.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const auto caller=LoadLibraryExW(bridge.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!core||!nr||!caller)throw std::runtime_error("runtime LoadLibrary failed: "+std::to_string(GetLastError()));
    auto init=Entry<Result(*)(void*,uint64_t,const wchar_t*,ID3D12Device*,uint32_t,const void*)>(caller,"NRChain_Init");
    auto create=Entry<Result(*)(void*,ID3D12GraphicsCommandList*,int,void*,void**)>(caller,"NRChain_Create");
    auto evaluate=Entry<Result(*)(void*,ID3D12GraphicsCommandList*,const void*,const void*,void*)>(caller,"NRChain_Evaluate");
    auto release=Entry<Result(*)(void*,void*)>(caller,"NRChain_Release");auto allocate=Entry<Result(*)(Params**)>(core,"NVSDK_NGX_D3D12_AllocateParameters");
    auto destroy=Entry<Result(*)(Params*)>(core,"NVSDK_NGX_D3D12_DestroyParameters");
    auto signedInit=Entry<void*>(nr,"NVSDK_NGX_D3D12_Init_Ext"),signedCreate=Entry<void*>(nr,"NVSDK_NGX_D3D12_CreateFeature"),signedEval=Entry<void*>(nr,"NVSDK_NGX_D3D12_EvaluateFeature"),signedRelease=Entry<void*>(nr,"NVSDK_NGX_D3D12_ReleaseFeature");
    Ngx(init(signedInit,0xE658703ull,output.c_str(),d.device.Get(),19,nullptr),"init");Params*p=nullptr;Ngx(allocate(&p),"allocate");if(!p)throw std::runtime_error("null params");
    auto c=Texture(d,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT),z=Texture(d,w,h,DXGI_FORMAT_R32_FLOAT),mv=Texture(d,w,h,DXGI_FORMAT_R16G16_FLOAT),out=Texture(d,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT);
    Upload(d,c.Get(),4,false);Upload(d,z.Get(),1,true);Upload(d,mv.Get(),2,false);Barrier(d,out.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Bind(p,w,h,c.Get(),z.Get(),mv.Get(),out.Get());void*handle=nullptr;Result created;
#ifdef NR_HOST_BACKEND
    nr::nvfp4::OwnedCall call;call.phase=nr::nvfp4::Phase::Create;call.context_id=1;call.generation=1;call.recording_token=100;call.device=d.device.Get();call.command_list=d.list.Get();call.width=w;call.height=h;
    {nr::nvfp4::OwnedScope scope(call);created=create(signedCreate,d.list.Get(),18,p,&handle);}
#else
    created=create(signedCreate,d.list.Get(),18,p,&handle);
#endif
    d.Submit(100);Ngx(created,"create");if(!handle)throw std::runtime_error("null handle");
    ComPtr<ID3D12QueryHeap>qh;D3D12_QUERY_HEAP_DESC qd{};qd.Count=2;qd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;Check(d.device->CreateQueryHeap(&qd,IID_PPV_ARGS(&qh)),"timestamps");auto times=Buffer(d,16,D3D12_HEAP_TYPE_READBACK);UINT64 frequency;Check(d.queue->GetTimestampFrequency(&frequency),"timestamp frequency");
#ifdef NR_HOST_BACKEND
    observation.frequency=frequency;
    if(!profile_grouped){qd.Count=128;Check(d.device->CreateQueryHeap(&qd,IID_PPV_ARGS(&observation.query)),"stage timestamps");observation.readback=Buffer(d,128*8,D3D12_HEAP_TYPE_READBACK);}
    if(profile_original){qd.Count=profile_stages?5:profile_range?2:Observation::OriginalCapacity*2;Check(d.device->CreateQueryHeap(&qd,IID_PPV_ARGS(&observation.original_query)),"original launch timestamps");observation.original_readback=Buffer(d,uint64_t(qd.Count)*8,D3D12_HEAP_TYPE_READBACK);}
    bool prior_replacement=false;
#endif
    const bool candidate=mode==L"reference"||mode==L"optimized"||mode==L"fused"||mode==L"two";
    bool epoch_started=!candidate;unsigned preparation_frames=0;
    std::ofstream original_launches_file,original_summary_file;
    if(profile_original){if(!profile_grouped)original_launches_file.open(output/"original-launches.jsonl");original_summary_file.open(output/(profile_stages?"original-stage-summary.jsonl":profile_range?"original-range-summary.jsonl":"original-launch-summary.jsonl"));
        if((!profile_grouped&&!original_launches_file)||!original_summary_file)throw std::runtime_error("original launch profile output open failed");
        original_launches_file<<std::setprecision(17);original_summary_file<<std::setprecision(17);}
    std::ofstream timing(output/"timing.jsonl");for(unsigned i=0,submission=0;i<frames+warmup;++submission){Int(p,"DLSSNR.Reset",!epoch_started||i==0);d.list->EndQuery(qh.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);Result evaluated;uint64_t pairs=0,observed=0,original_launches=0,replacement_launches=0,uav_barriers=0;std::string reason="disabled";
#ifdef NR_HOST_BACKEND
        observation.count=0;if(profile_original)observation.ResetOriginal();
        call.phase=nr::nvfp4::Phase::Evaluate;call.recording_token=101+submission;call.evaluation_serial=submission+1;
        {nr::nvfp4::OwnedScope scope(call);if(!epoch_started&&scope.WillReplace())epoch_started=true;if(scope.WillReplace()!=prior_replacement)Int(p,"DLSSNR.Reset",1);evaluated=evaluate(signedEval,d.list.Get(),handle,p,nullptr);const auto status=scope.Status();pairs=status.replaced_pairs;observed=status.observed_m;original_launches=status.original_launches;replacement_launches=status.replacement_launches;uav_barriers=status.uav_barriers;reason=status.reason;if(evaluated==1)prior_replacement=pairs>0;if(scope.MayHaveRecordedFailure())GpuFailure("backend partial recording");}
#else
        evaluated=evaluate(signedEval,d.list.Get(),handle,p,nullptr);
#endif
        d.list->EndQuery(qh.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);d.list->ResolveQueryData(qh.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,times.Get(),0);
#ifdef NR_HOST_BACKEND
        if(observation.count)d.list->ResolveQueryData(observation.query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,observation.count,observation.readback.Get(),0);
        if(profile_original&&observation.original_count)d.list->ResolveQueryData(observation.original_query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,observation.original_count*2,observation.original_readback.Get(),0);
        if(profile_range&&observation.range_began&&observation.range_ended)d.list->ResolveQueryData(observation.original_query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,observation.original_readback.Get(),0);
        if(profile_stages&&observation.stage_boundary_count==5)d.list->ResolveQueryData(observation.original_query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,5,observation.original_readback.Get(),0);
#endif
        d.Submit(101+submission);if(evaluated!=1)Ngx(evaluated,"evaluate");
        if(epoch_started&&candidate&&pairs!=8)throw std::runtime_error("candidate left the fully prepared measurement epoch");
        void*ptr;D3D12_RANGE range{0,16};Check(times->Map(0,&range,&ptr),"timestamp map");uint64_t ticks[2];memcpy(ticks,ptr,16);D3D12_RANGE written{0,0};times->Unmap(0,&written);if(ticks[1]<ticks[0])throw std::runtime_error("nonmonotonic timestamps");
        const double ms=double(ticks[1]-ticks[0])*1000.0/frequency;timing<<"{\"frame\":"<<i<<",\"submission\":"<<submission<<",\"phase\":\""<<(!epoch_started?"preparing":i<warmup?"warmup":"measured")<<"\",\"warmup\":"<<(!epoch_started||i<warmup?"true":"false")<<",\"gpu_complete\":true,\"feature18_ms\":"<<ms<<",\"replaced_pairs\":"<<pairs<<",\"M\":"<<observed<<",\"original_launches\":"<<original_launches<<",\"replacement_launches\":"<<replacement_launches<<",\"uav_barriers\":"<<uav_barriers;
#ifdef NR_HOST_BACKEND
        const auto stage_times=observation.Times();if(observation.count)timing<<",\"ffn_ms\":"<<(pairs?stage_times[1]:stage_times[0])<<",\"pack_ms\":"<<stage_times[2]<<",\"expansion_ms\":"<<stage_times[3]<<",\"contraction_ms\":"<<stage_times[4]<<",\"epilogue_ms\":"<<stage_times[5];
        if(profile_original)observation.WriteOriginal(original_launches_file,original_summary_file,i,submission,!epoch_started?"preparing":i<warmup?"warmup":"measured",ticks);
#endif
        timing<<"}\n";
        if(submission<3||(epoch_started&&(i<3||i%100==0)))std::cerr<<"frame="<<i<<" phase="<<(!epoch_started?"preparing":i<warmup?"warmup":"measured")<<" feature18_ms="<<ms<<" replaced_pairs="<<pairs<<" M="<<observed<<" reason="<<reason<<'\n';
        if(epoch_started)++i;else if(++preparation_frames>=4096)throw std::runtime_error("candidate preparation did not complete within the bounded warmup");}
    Dump(d,out.Get(),output/"nr_output.f16");Ngx(release(signedRelease,handle),"release");Ngx(destroy(p),"destroy params");
#ifdef NR_HOST_BACKEND
    nr::nvfp4::RetireContext(1,1);nr::nvfp4::Shutdown();
#endif
    std::ofstream meta(output/"host.json");meta<<"{\"width\":"<<w<<",\"height\":"<<h<<",\"frames\":"<<frames<<",\"warmup\":"<<warmup<<",\"preparation_frames\":"<<preparation_frames<<",\"history_epoch\":\"reset-after-candidate-ready-v1\",\"mode\":\""<<std::string(mode.begin(),mode.end())<<"\",\"input\":\"synthetic-static-fp16\",\"timing_boundary\":\"Feature18-only\",\"original_launch_profile\":"<<(profile_original?"true":"false")<<",\"profile_is_instrumented\":"<<(profile_original?"true":"false")<<",\"original_input_range\":";
    if(profile_range)meta<<'"'<<range_first<<':'<<range_last<<'"';else meta<<"null";
    meta<<",\"original_fixed_stages\":"<<(profile_stages?"true":"false")<<",\"game\":false,\"sr\":false}\n";
    std::cout<<"completed=true game=false sr=false frames="<<frames<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"host_failed="<<e.what()<<'\n';std::cerr.flush();ExitProcess(2);}}
