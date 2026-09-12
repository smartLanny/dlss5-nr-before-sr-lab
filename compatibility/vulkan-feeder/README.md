# Vulkan external-provider transport

This source adapter preserves the existing ReShade Vulkan profile/layer and its
same-LUID Vulkan-to-D3D12 transport while replacing the old Core payload through
`NRExternalProviderV1`. `apply.py` accepts only the locked upstream Feeder source
and applies `d15-provider.patch`; it does not install a Core, `nrchain_nvngx.dll`,
or an NVIDIA NR runtime.

The D15 delta declares the exact future fence value signalled by `EndCommands`,
makes both same-device and Vulkan FP16 outputs render-target capable, and refuses
to release transport resources while Core still owns unfinished exact-completion
work. The Vulkan input/same-frame/output waits and original-frame fallback remain
the existing transport implementation.

Build the generated source with its `build.bat` after supplying the same official
NGX and Vulkan header inputs required by upstream Feeder. Compilation alone does
not claim Vulkan game, HDR, visual-quality, or GPU completion acceptance.
