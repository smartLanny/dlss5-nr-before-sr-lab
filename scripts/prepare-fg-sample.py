"""Instrument a pinned external NVIDIA sample; never vendor its SDK or renderer."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

SAMPLE = 'dd6e1803b97e5b3f218f5776d85f09e4c386c34f'


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Pinned sample instrumentation anchor changed: '+old[:70])
    return text.replace(old,new,1)


def legacy_transform(name, text):
    if name == 'src/SLWrapper.cpp':
        text=text.replace('pref.showConsole = true;', 'pref.showConsole = false;')
        text=once(text,'    sl::Preferences pref;', '''    sl::Preferences pref;
    // Reproducible sample only: do not request OTA or select cached plugins.
    pref.flags = sl::PreferenceFlags::eDisableCLStateTracking;''')
        text=once(text,'    m_dlssg_available = slIsFeatureSupported(sl::kFeatureDLSS_G, adapterInfo) == sl::Result::eOk;', '''    const auto fg_support = slIsFeatureSupported(sl::kFeatureDLSS_G, adapterInfo);
    log::info("FG_SAMPLE_CAPABILITY result=%u end=1", unsigned(fg_support));
    m_dlssg_available = fg_support == sl::Result::eOk;''')
        text=once(text,'    successCheck(slDLSSGGetState(m_viewport, m_dlssg_settings, &m_dlssg_consts), "slDLSSGGetState");', '''    const auto state_result = slDLSSGGetState(m_viewport, m_dlssg_settings, &m_dlssg_consts);
    successCheck(state_result, "slDLSSGGetState");
    log::info("FG_SAMPLE_LIMITS result=%u minimum=%u maximumGenerated=%u end=1", unsigned(state_result),
        m_dlssg_settings.minWidthOrHeight, m_dlssg_settings.numFramesToGenerateMax);''')
        text=once(text,'    fps_multiplier = m_dlssg_settings.numFramesActuallyPresented;', '''    fps_multiplier = m_dlssg_settings.numFramesActuallyPresented;
#if DONUT_WITH_DX12
    auto *device = static_cast<ID3D12Device*>(m_Device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device));
    const HRESULT removed = device ? device->GetDeviceRemovedReason() : E_POINTER;
    auto *inputFence = static_cast<ID3D12Fence*>(m_dlssg_settings.inputsProcessingCompletionFence);
    const uint64_t completed = inputFence ? inputFence->GetCompletedValue() : 0;
    const uint64_t target = m_dlssg_settings.lastPresentInputsProcessingCompletionFenceValue;
    const bool inputComplete = SUCCEEDED(removed) && inputFence && target != 0 &&
        completed != ~uint64_t{0} && target != ~uint64_t{0} && completed >= target;
    log::info("FG_SAMPLE_STATE mode=%u generatedRequested=%u presented=%u status=%u fence=%llu completed=%llu inputComplete=%u deviceResult=0x%08x vram=%llu end=1",
        unsigned(m_dlssg_consts.mode), m_dlssg_consts.numFramesToGenerate,
        m_dlssg_settings.numFramesActuallyPresented, unsigned(m_dlssg_settings.status),
        (unsigned long long)target, (unsigned long long)completed, unsigned(inputComplete),
        unsigned(removed), (unsigned long long)m_dlssg_settings.estimatedVRAMUsageInBytes);
#endif''')
    elif name == 'src/main.cpp':
        text=once(text,'    std::string sceneName;', '    donut::log::EnableOutputToMessageBox(false);\n    std::string sceneName;')
        # The upstream callback blindly drops the final character even when
        # there is no newline, corrupting numeric evidence ending in zero.
        text=once(text,'log_file << str.substr(0, str.size() - 1).c_str() << std::endl;',
                  'log_file << str << std::endl;')
    elif name == 'donut/src/app/DeviceManager.cpp':
        text=once(text,'    glfwShowWindow(m_Window);','''    glfwShowWindow(m_Window);
    // DLSS-G intentionally suppresses interpolation for an unfocused window.
    // This bounded sample explicitly needs presentation focus, not a popup.
    glfwFocusWindow(m_Window);''')
    elif name == 'src/StreamlineSample.cpp':
        text=once(text,'    // CLOSE: ', '''    donut::log::info("FG_SAMPLE_SOURCE frame=%u animated=%u focused=%u end=1",
        unsigned(GetFrameIndex()), unsigned(m_ui.EnableAnimations), unsigned(glfwGetWindowAttrib(GetDeviceManager()->GetWindow(), GLFW_FOCUSED)));
    // CLOSE: ''')
    return text


def transform(name, text):
    text = legacy_transform(name, text)
    if name == 'src/SLWrapper.cpp':
        text = once(text, '    sl::Preferences pref;', '''    log::info("FG_SAMPLE_DIAGNOSTICS revision=2 stateQueriesPerSourceFrame=1 vramEstimate=once end=1");
    sl::Preferences pref;''')
        text = text.replace('pref.logLevel = sl::LogLevel::eDefault;',
                            'pref.logLevel = sl::LogLevel::eVerbose;')
        text = once(text, '    const auto state_result = slDLSSGGetState(m_viewport, m_dlssg_settings, &m_dlssg_consts);', '''    // One consuming state query per source frame, on the original render thread.
    // Estimate once from actual renderer descriptors; never interpret an unrequested zero.
    static bool estimateRequested = false;
    auto queryOptions = m_dlssg_consts;
    const bool requestEstimate = !estimateRequested && queryOptions.numBackBuffers &&
        queryOptions.mvecDepthWidth && queryOptions.colorWidth;
    if (requestEstimate) queryOptions.flags |= sl::DLSSGFlags::eRequestVRAMEstimate;
    const auto state_result = slDLSSGGetState(m_viewport, m_dlssg_settings,
        requestEstimate ? &queryOptions : nullptr);
    if (requestEstimate) {
        estimateRequested = true;
        log::info("FG_SAMPLE_VRAM_ESTIMATE requested=1 result=%u bytes=%llu end=1",
            unsigned(state_result), (unsigned long long)m_dlssg_settings.estimatedVRAMUsageInBytes);
    }''')
        text = once(text, '    successCheck(slDLSSGSetOptions(m_viewport, m_dlssg_consts), "slDLSSGSetOptions");', '''    m_dlssg_consts.onErrorCallback = [](const sl::APIError& error) {
        log::error("FG_SAMPLE_API_ERROR hresult=0x%08x end=1", unsigned(error.hres));
    };
    successCheck(slDLSSGSetOptions(m_viewport, m_dlssg_consts), "slDLSSGSetOptions");''')
    elif name == 'src/StreamlineSample.cpp':
        text = once(text, '''        auto fenceValue = lastPresentDLSSGInputsProcessingFenceValue;
        // This is where we query DLSS-G FPS, estimated VRAM usage and status
        NVWrapper::Get().QueryDLSSGState(estimatedVramUsage, fps_multiplier, status, minSize, numFramesMaxMultiplier, pDLSSGInputsProcessingFence, lastPresentDLSSGInputsProcessingFenceValue, vsyncSupported, bIsDynamicMFGSupported);
        assert(fenceValue == lastPresentDLSSGInputsProcessingFenceValue);''', '''        // Reuse this frame's query: another GetState consumes the since-query
        // presentation counter a second time and splits asynchronous presents.''')
        text = once(text, '        dlssgConst.mode = m_ui.DLSSG_mode;', '''        dlssgConst.mode = m_ui.DLSSG_mode;
#if DONUT_WITH_DX12
        // The bounded D3D12 probe retains fixed dimensions. On the first frame
        // resources do not yet exist; estimate on the next query after creation.
        if (m_RenderTargets && GetDevice()->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12) {
            auto format = [](nvrhi::ITexture* texture) {
                return unsigned(static_cast<ID3D12Resource*>(texture->getNativeObject(
                    nvrhi::ObjectTypes::D3D12_Resource))->GetDesc().Format);
            };
            dlssgConst.numBackBuffers = GetDeviceManager()->GetBackBufferCount();
            dlssgConst.mvecDepthWidth = m_RenderTargets->Depth->getDesc().width;
            dlssgConst.mvecDepthHeight = m_RenderTargets->Depth->getDesc().height;
            dlssgConst.colorWidth = backbufferWidth;
            dlssgConst.colorHeight = backbufferHeight;
            dlssgConst.colorBufferFormat = format(framebuffer->getDesc().colorAttachments[0].texture);
            dlssgConst.depthBufferFormat = format(m_RenderTargets->Depth);
            dlssgConst.mvecBufferFormat = format(m_RenderTargets->MotionVectors);
            dlssgConst.hudLessBufferFormat = format(m_RenderTargets->PreUIColor);
            dlssgConst.uiBufferFormat = format(m_RenderTargets->UIColorAlpha);
        }
#endif''')
    return text


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sample',type=Path,required=True)
    ap.add_argument('--manifest',type=Path,required=True)
    a=ap.parse_args()
    head=subprocess.check_output(['git','-C',str(a.sample),'rev-parse','HEAD'],text=True).strip()
    if head != SAMPLE: raise ValueError('Wrong official sample revision')
    changes={}
    for name in ('src/SLWrapper.cpp','src/main.cpp','src/StreamlineSample.cpp','donut/src/app/DeviceManager.cpp'):
        original=subprocess.check_output(['git','-C',str(a.sample),'show','HEAD:'+name]).decode('utf-8')
        original=original.replace('\r\n','\n')
        patched=transform(name,original)
        current=(a.sample/name).read_text(encoding='utf-8')
        if current not in (original,legacy_transform(name,original),patched): raise ValueError('Unrelated sample edit: '+name)
        changes[name]={'upstream_sha256':hashlib.sha256(original.encode()).hexdigest(),
                       'patched_sha256':hashlib.sha256(patched.encode()).hexdigest()}
    for name in changes:
        original=subprocess.check_output(['git','-C',str(a.sample),'show','HEAD:'+name]).decode('utf-8').replace('\r\n','\n')
        (a.sample/name).write_text(transform(name,original),encoding='utf-8',newline='\n')
    a.manifest.write_text(json.dumps({'sample_commit':head,'patches':changes,
        'nr_enabled':False,'sdk_ota_enabled':False,'focus_requested':True},indent=2)+'\n',encoding='utf-8')
    print('Prepared pinned FG-only sample instrumentation; no runtime result implied.')


if __name__ == '__main__':main()
