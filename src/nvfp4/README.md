# Scoped NVFP4 backend

The default is disabled. Production Auto also remains FP8 because
`policy_registry.h` contains no qualified entry. Adding a reviewed policy after
GPU numerical, image and performance acceptance requires no Core integration
change. The registry binds the actual NR runtime, adapter/driver profile,
artifact pair, Params build ID, tested shapes and qualification artifact hash.
An arbitrary asset-directory JSON file or `allow_auto` flag cannot activate it.
The current profile's CUDA driver API version is a compatibility number, not
the actual NVIDIA driver build (for example, 610.62). Before adding the first
qualified entry, its qualification profile must additionally lock the real
NVIDIA driver build and any necessary driver binary identities. The empty
registry currently admits no adapter or driver.

`UpstreamReference` and `Optimized` are explicit host experiment modes. The
reference retains the pinned upstream three contraction templates (M960,
M2160, M3840), four launches and five UAV dependencies per pair. The optional
fused candidate records three launches and four UAV dependencies; its real GPU
numerical and performance validation is pending. No wider reference whitelist
is inferred from a builder accepting a shape.

Every replacement is restricted to this plugin's pre-SR Feature18 scope,
blocks 31–38, the approved signed NR runtime SHA-256, the exact original module
digest, the locked assets, the matching D3D12/CUDA adapter LUID and actual SM120
capability/SM count. Unknown functions and all other feature scopes retain the
original NVAPI call. The current approved runtime digest is
`e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e`.

Cold preparation loads assets, allocates buffers, uploads weights, matches CUDA
devices and builds cached Params on a worker. The owned Evaluate freezes its
lease before the first launch. It remains entirely FP8 when that lease is not
ready. No recording path allocates, maps a resource, creates a descriptor or
waits for GPU completion. The optional fused path may update only the explicit
48-byte linear binding region reported by its trusted builder. It never
patches opaque TMA data.

The 256 MiB device budget counts committed allocation sizes, including upload
buffers. A recording retains its arena through queue submission, every known
fence, replay, and successful Reset. Unknown submission ownership is permanently
quarantined. A known post-recording failure affects that context and permits no
same-list retry; known completed/reset resources can retire. Retired contexts
are removed, with a bounded exact recent-retirement guard. Context IDs are
owner-generated and must never be reused. Idle devices are destroyed only after
their contexts, uploads, recordings and preparation jobs retire. A failed CUDA
object destruction retains all remaining device dependencies without retry.

The worker and NVAPI hook owners are pinned before installation. `Shutdown`
closes admission; it does not invalidate recorded work or unload callback code.
Stage callbacks are optional private diagnostics and are null in production.

Run `tests/nvfp4_backend/run.ps1` with the configured LLVM/MinGW toolchain for
CPU-only contract tests. They execute the actual backend with fake recording
surfaces, cover all 32 reference and 24 fused launch failure positions, assert
zero steady-state heap allocations, exercise Auto gating, runtime hash rejection,
5,000 context retirements, and distinguish fence completion from list Reset.
They do not claim CUDA, WARP, image or performance acceptance.

Reference asset identities and the original FFN ABI were checked against
[the pinned OptiScaler source](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/tree/ec96f42f8d32a4fc82f7c4558c0ed12a9f90d853/OptiScaler/dlssnr)
and [NVIDIA's NVAPI declarations](https://github.com/NVIDIA/nvapi/blob/main/nvapi.h).
Vendor binaries, private captures and trusted experimental artifact files are
kept outside tracked source.
