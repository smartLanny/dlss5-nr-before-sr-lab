# Actual external Core NVIDIA host experiment

The production consumer now has a bounded NVIDIA execution result in addition
to the earlier Windows/WARP evidence. This is **not a game or full Feeder addon
acceptance**: the host provides test-only ReShade registration, never invokes UI
or Present, and supplies an artificial gradient, constant depth and zero motion.
There is no fake NGX implementation and no change to product DllMain.

## Two concrete blockers found and fixed

1. The recovered producer skipped official NGX initialization when claiming the
   external core. The core's allocator requires an already loaded `_nvngx.dll`,
   which a pure non-DLSS host cannot assume. The producer now reuses its existing
   official SafeNgxInit12 bootstrap and tracks shutdown normally, while skipping
   private Feature1 creation/evaluation and private parameters. Initialization
   failure unwinds the session. No driver DLL is loaded from a guessed path.
2. On real NVIDIA hardware, initialization succeeded, but Feature18 Create
   received `0x0` and returned `0xBAD00005` (InvalidParameter). The recovered
   consumer had not populated the current compositor's `work_width/work_height`.
   It now forwards both full output size and model work size. This is a Core
   integration bug found by execution, not an inferred driver limitation.

These production fixes are source
`53fb7fb5bfee49bf37bfc7d50a45f62a54182088`.

## Actual results

The host selects the NVIDIA adapter by vendor ID, uses the official SDK static
library for Init, and calls the production DLL's four provider exports.
The local GPU was RTX 5090 Laptop (driver 610.62). Standard NGX Init returned
`0x00000001`; `_nvngx.dll` was loaded. The existing signed NR runtime and project's
nrchain were copied into a new private experiment directory, never into a game.

| Scenario | Model/output contract | Result |
| --- | --- | --- |
| WARP off | Master disabled | Actual Submit accepted identity; recorded=0; marker pixels retained; owner released |
| WARP no runtime, explicit linear R8 profile | Valid storage, missing modules | Actual Submit declined runtime-unavailable; original marker pixels retained |
| NVIDIA before extent fix | Actual Feature18 receives 0x0 | Create rejected InvalidParameter; no completed NR claimed |
| NVIDIA after fix, 100% | R/O 640x360, N 640x360 | 2 affinity-training frames, 2 warmup frames, then 4 real NR + compositing frames, each completed and read back |
| NVIDIA after fix, 50% request | R/O 640x360, aligned N 320x184 | Same sequence; 4 real NR + compositing frames, each completed and read back |

At 100%, the four completed outputs differed from the input at
230336 / 230330 / 230341 / 230323 pixels. Their fixture FNV hashes were
937F62C3 / 3AEE3B44 / 6408475B / 2225D8A3. The two warmup outputs matched the
input exactly. At 50%, all 230400 pixels differed after warmup; hashes were
4D578C41 / 8B67E5F7 / EE268AC1 / D5B31CE0. Core GPU diagnostics reported zero
invalid sampled values. Pixel change proves the real Core path produced and
composited model output; it does not establish image quality or temporal quality
on game scenes.

The final host uses the production bounded fence completion helper, checks
readback pixels, and exits nonzero for a hardware experiment with no changed
recorded frame. Test source is `aa0ac0daa2ef0bc770e75cd0a8c3392c63d46f81`.
No copied test function substitutes for production consumer Submit or NGX.

## Binary identities and reproduction

Production Core DLL source: `53fb7fb`; SHA256
`081c61f293710face535f0cdcf9011cdc257e6d319595dfb99ae2fb064627a18`.
Full Feeder producer including official bootstrap compiled/linked with MSVC
from `aa0ac0d`; SHA256
`52b70cc7ad4512740053438fdbed491b496bc23c81f7f7fefe2aeedfd20c850f`.
The Feeder binary's full ReShade callback path was not run in this fixture.

The private `provider-transport-20260908` archive retains `hardware-validation.json`,
the `host-verified100`, `host-verified50`, `host-verified-off`, `host-on-linear`
and initial `host-nvidia` experiment directories, and `msvc-bootstrap/validation.json`.
The first `host-on` attempt was rejected by unknown R8 storage before loading
runtime; the explicit-linear `host-on-linear` case reaches and records the
missing-module failure. These are distinct negative controls.

Build `tests/multi_api/external_consumer_host.cpp` with MSVC, the official SDK
headers/library, and `NR_HOST_NGX_BOOTSTRAP=1`. No SDK files are tracked here.
Place the production core, matching nrchain and existing signed NR runtime in
a fresh private directory. Configure Enabled=1, Mode=2, R8OutputEncoding=1,
WorkMode=0 for 100%, or WorkMode=5 / CustomWorkScale=0.5 for 50%. Run the host
with the absolute core DLL path and `hardware`. `off` and `on` use WARP and
exercise disabled / unavailable behavior. The host does not modify configuration
or install anything; it reads the explicitly prepared fixture directory.

## Remaining boundary

An actual Feeder/ReShade callback chain must still prove guide lookup, queue
ordering with the game's immediate list, same-frame game copyback, reload,
resize, shutdown/recovery and absence of a second NR owner. Native DLSS stays on
the existing direct route, with no Opti dependency. Native FSR/XeSS extraction,
real SR injection, FG presentation, Vulkan and game quality remain independent.
The existing 40 native regression gates apply to their recorded `48598dc` build;
they are not relabelled as this hardware test. The actual-source fault/transform
tests and WARP transport were rerun after these changes. #164 remains open.
