// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace quickbar {
// Minimal declarations for the documented NVML C ABI; no NVIDIA binary is shipped.
using Device=void*;
struct Utilization { unsigned gpu, memory; };
struct Memory { unsigned long long total, free, used; };
struct GpuReading {
    std::string name;
    float utilization=-1,power_w=-1,temperature_c=-1,memory_mib=-1;
    unsigned long long sample_tick=0;
    bool ready=false;
};
struct GpuSnapshot { std::vector<std::string> names; int selected=-1; GpuReading reading; std::string reason="等待监测"; };
class NvmlMonitor {
    std::mutex mutex_;std::condition_variable wake_;
    bool stop_=false,wanted_=false;int selection_=-1;GpuSnapshot snapshot_;
    std::thread worker_; // Start only after all state members have been initialized.
    template<class T>static T proc(HMODULE m,const char*n){return reinterpret_cast<T>(GetProcAddress(m,n));}
    static HMODULE load_system_nvml(){
        wchar_t sys[32768]{};UINT len=GetSystemDirectoryW(sys,32768);
        if(!len || len>=32768)return nullptr;
        std::wstring file(sys);file+=L"\\nvml.dll";
        return LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    void run(){
        HMODULE module=nullptr;bool initialized=false;
        using Init=int(*)();using Count=int(*)(unsigned*);using Get=int(*)(unsigned,Device*);
        using Name=int(*)(Device,char*,unsigned);using U=int(*)(Device,Utilization*);
        using P=int(*)(Device,unsigned*);using T=int(*)(Device,unsigned,unsigned*);using M=int(*)(Device,Memory*);
        Init shutdown=nullptr;U util=nullptr;P power=nullptr;T temp=nullptr;M memory=nullptr;
        std::vector<Device> devices;std::vector<std::string> names;
        auto retry_at=std::chrono::steady_clock::time_point{};
        for(;;){
            bool wanted;int selected;
            {std::unique_lock<std::mutex> l(mutex_);wake_.wait_for(l,std::chrono::milliseconds(1000));if(stop_)break;wanted=wanted_;selected=selection_;}
            if(!wanted)continue;
            if(!initialized && std::chrono::steady_clock::now()>=retry_at){
                retry_at=std::chrono::steady_clock::now()+std::chrono::seconds(30);
                if(module)FreeLibrary(module);module=load_system_nvml();
                auto init=module?proc<Init>(module,"nvmlInit_v2"):nullptr;
                auto count=module?proc<Count>(module,"nvmlDeviceGetCount_v2"):nullptr;
                auto get=module?proc<Get>(module,"nvmlDeviceGetHandleByIndex_v2"):nullptr;
                auto name=module?proc<Name>(module,"nvmlDeviceGetName"):nullptr;
                shutdown=module?proc<Init>(module,"nvmlShutdown"):nullptr;
                if(init&&count&&get&&name&&shutdown && init()==0){
                    initialized=true;unsigned n=0;devices.clear();names.clear();
                    if(count(&n)==0 && n<=16)for(unsigned i=0;i<n;++i){Device d=nullptr;char text[128]{};
                        if(get(i,&d)==0 && name(d,text,sizeof(text))==0){devices.push_back(d);names.emplace_back(text);}}
                    util=proc<U>(module,"nvmlDeviceGetUtilizationRates");power=proc<P>(module,"nvmlDeviceGetPowerUsage");
                    temp=proc<T>(module,"nvmlDeviceGetTemperature");memory=proc<M>(module,"nvmlDeviceGetMemoryInfo");
                }
                std::lock_guard<std::mutex> l(mutex_);snapshot_.names=names;
                snapshot_.reason=initialized?"多显卡：请选择监测设备":"系统 NVML 不可用";
            }
            if(!initialized)continue;
            // No assumption that NVML index equals the game's DXGI adapter index.
            if(selected<0 && devices.size()==1)selected=0;
            if(selected<0 || static_cast<size_t>(selected)>=devices.size())continue;
            GpuReading r;r.name=names[selected];r.sample_tick=GetTickCount64();
            unsigned p=0,t=0;Utilization u{};Memory m{};Device d=devices[selected];
            if(util&&util(d,&u)==0&&u.gpu<=100)r.utilization=static_cast<float>(u.gpu);
            if(power&&power(d,&p)==0)r.power_w=p/1000.f;
            if(temp&&temp(d,0,&t)==0)r.temperature_c=static_cast<float>(t);
            if(memory&&memory(d,&m)==0)r.memory_mib=static_cast<float>(m.used/1048576.0);
            r.ready=r.utilization>=0||r.power_w>=0||r.temperature_c>=0||r.memory_mib>=0;
            {std::lock_guard<std::mutex> l(mutex_);snapshot_.selected=selected;snapshot_.reading=r;snapshot_.reason=r.ready?"设备总体；非游戏独占":"驱动不支持当前传感器";}
        }
        if(initialized&&shutdown)shutdown();if(module)FreeLibrary(module);
    }
public:
    NvmlMonitor():worker_([this]{try{run();}catch(...){std::lock_guard<std::mutex> l(mutex_);snapshot_.reading={};snapshot_.reason="传感器不可用";}}){}
    ~NvmlMonitor(){{std::lock_guard<std::mutex> l(mutex_);stop_=true;}wake_.notify_all();if(worker_.joinable())worker_.join();}
    void request(bool enabled){std::lock_guard<std::mutex> l(mutex_);if(wanted_!=enabled){wanted_=enabled;wake_.notify_all();}}
    void select(int index){std::lock_guard<std::mutex> l(mutex_);selection_=index;snapshot_.reading={};wake_.notify_all();}
    GpuSnapshot copy(){std::lock_guard<std::mutex> l(mutex_);return snapshot_;}
};
}
