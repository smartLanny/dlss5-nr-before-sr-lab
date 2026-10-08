# Minimal scene bench, native host r2 / CPU exporter r3

This is an own-source review subset, not a redistributable vendor SDK/runtime or a self-contained product build. It replaces the blocked official Sample initialization seam with the previously successful typed D3D12/NGX host. The scene is an analytic Cornell room, two opaque boxes, and a thin bar, using a sampled ceiling area light. Radiance, hardware depth, normalized world normals, linear diffuse albedo and roughness come from the same jittered hit. The static camera/geometry use actual zero motion; specular albedo is zero and roughness is one.

The full Core translation unit is compiled into the host from commit `45df68dc80138e89778e544d49f15109ec437a4f`, src tree `7fe3819be240c0fad0eb16526e9d4b3ce0a67f87`. This is **not** proof that a loaded addon DLL is the current candidate. Actual Feature18 NR is `a2d16f9fba2b619559427821169918df56fc8e410a6ac87569953598a00b66fa`; native RR is `50eb21e654a1fd12403e000f5ac9c2488bf9149930c028e73196f3f1d47d10a9`. This is not NRD denoising.

The research route is raw low-resolution NR → one native RR. Product default RR → NR remains unchanged. SR and the production HookCoreEvaluate entry are not admitted by this version. Input/NR are 1280×720, output 1920×1080, one NR layer. There is no fake hit distance or second externally accumulated temporal edit.

## Actual execution

Four fresh processes completed in order: capture NR off/on, then perf NR off/on. Each ran 120 warmup + 120 observed frames with the same camera, frame IDs, input noise seed and jitter schedule. Each on arm recorded 240 Feature18 evaluations and 238 actual combines; every arm recorded 240 original RR evaluations and exact completed fence + successful Reset retirement. Perf arms allocate/record no image readback or diagnostic image; only two timestamps and a 16-byte timestamp readback accompany the existing queue. No other development GPU test or compilation ran in the performance window.

The original CPU export selected an absent libx264 encoder and failed after all native arms had passed. Raw captures and native results were preserved. A separate CPU-only export used the installed libvpx-vp9 encoder; no GPU arm was repeated. Runner r3 checks that encoder before starting native work. Raw 120-frame HDR captures, fixed-transform WebM videos and identical nearest-neighbor crops are retained locally. Capture timings are excluded from the reported performance comparison.

| Controlled research arm | GPU median / P95 | Process local VRAM stable / sampled peak | After exact retirement | After model release | After pool release |
| --- | --- | --- | --- | --- | --- |
| NR off | 0.849536 / 0.854304 ms | 411.65625 MiB | 411.65625 MiB | 411.65625 MiB | 349.78125 MiB |
| NR on | 3.327488 / 3.938240 ms | 849.9296875 MiB | 849.9296875 MiB | 827.30078125 MiB | 765.42578125 MiB |

GPU scope is scene + Core + final native RR on the graphics queue, excluding image copy and Present. This serial research host has CPU checks, logging and per-frame waits; inverse GPU milliseconds is not game FPS. DXGI local memory is process usage sampled at boundaries, not instantaneous allocation maximum. A retained driver/model cache after handle release is not evidence by itself of a leak. This run does not establish a 0.4.7 game regression or a private-SR quality benefit.

Native packet SHA256: `ce2892176df7c5608f8465ffc7ce4d7b23a6ef1c97e34c9201d9c177fc6f7c9c`.

Native executable SHA256: `4009f2d30352b72f4c704c464af821b5d387e1812a66ac05aaee75c6c65b434e`.

Actual result SHA256, capture off/on then perf off/on:

- `b401477c1b091ddf88bf4109491f56ce09167158363dbe1d1400fd1354f4d1f7`
- `7ac7ad8437a679cbf628f3b0127084448a7ba2bc6dbeb18a36113e8fbcd18cd7`
- `2cb53a081c474a5faaed652986f1292430a5e2788bf8ada63d16ca3772d4916f`
- `7ee154630acd8fd5cf5b7bf075acdf00aa6056920416f87406cb297695e103c2`

Current runner/UI packet SHA256: `0dd908ca33711076ff9fa8bf5681fb5aa7853f5f16d70fe03b007e88fd5484f4` (CPU export-only changes; native executable unchanged).

Initial three-frame readback checks and image review passed without black output or an obvious on-only defect; the mature 120-warmup image review is recorded separately. No game or general temporal quality acceptance is claimed.
