// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <psapi.h>
#include <string>
#include "../shared/nrui_api.h"
namespace quickbar {
class CoreBridge {
    HMODULE held_=nullptr;NrUiGetV1 get_=nullptr;NrUiSetV1 set_=nullptr;
    unsigned long long probe_at_=0;
public:
    NrUiStateV1 state{};bool available=false;const char* message="等待支持控制接口的 NR 核心";
    ~CoreBridge(){if(held_)FreeLibrary(held_);}
    void probe(){
        if(held_ || GetTickCount64()<probe_at_)return;probe_at_=GetTickCount64()+2000;
        HMODULE modules[512]{};DWORD needed=0;
        if(!EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&needed)||needed>sizeof(modules))return;
        HMODULE found=nullptr;unsigned count=0;
        for(unsigned i=0;i<needed/sizeof(HMODULE);++i){wchar_t file[32768]{};
            if(!GetModuleFileNameW(modules[i],file,32768))continue;std::wstring name(file);
            if(name.size()<8 || _wcsicmp(name.c_str()+name.size()-8,L".addon64"))continue;
            if(GetProcAddress(modules[i],"NRUI_GetStateV1")&&GetProcAddress(modules[i],"NRUI_SetValueV1")){found=modules[i];++count;}}
        if(count!=1){message=count>1?"检测到多个 NR 核心，已禁用控制":"需配套 Quickbar 核心构建；请用 Home 原设置页";return;}
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(found),&held_))return;
        get_=reinterpret_cast<NrUiGetV1>(GetProcAddress(held_,"NRUI_GetStateV1"));
        set_=reinterpret_cast<NrUiSetV1>(GetProcAddress(held_,"NRUI_SetValueV1"));
    }
    bool read(){probe();if(!get_)return false;NrUiStateV1 next{};next.size=sizeof(next);next.version=1;
        uint32_t r=get_(&next);available=r==NRUI_OK;if(available){state=next;message=next.capture_busy?"F8 抓帧中，设置暂时锁定":"已连接 NR 核心";}else message="核心忙，暂不接收设置";return available;}
    uint32_t write(uint32_t key,float value){
        if(!available||!set_)return NRUI_UNSUPPORTED;
        auto r=set_(key,value,state.generation);read();
        if(r==NRUI_CONFLICT)message="设置已由其他入口修改，请重试";
        if(r==NRUI_BUSY)message="抓帧或配置处理期间暂不能修改";
        if(r==NRUI_IO_ERROR)message="已应用但保存失败，请检查目录权限";
        return r;
    }
};
}
