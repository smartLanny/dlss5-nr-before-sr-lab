# D15 development build and format/exposure validation

The candidate identity is `beta0.5-dline15`, numeric Core PE `0.5.1.15`.
Development starts from `fb889543fb95bc4064dd2bee9ec57c75099b8679` and preserves
the D14 artifacts. A development snapshot is a frozen image of working files,
not a claim that its Git parent contains those changes.

## Source generation

`scripts/prepare-development-snapshot.py` reads the explicit tracked
`src`, `scripts`, `config`, and `third_party` file list. Each new untracked
production source must be supplied separately with `--extra-source src/<name>`.
An existing local source include omitted from the list is rejected. The script
checks that the source files and Git parent do not change while copying.

The snapshot reuses the canonical transformations for the native bridge,
post-SR integration, typed format access, exposure, and source identity. It
creates no commit and does not change the index. `input-manifest.json` records
every original SHA-256; the generated manifest records the complete staged
source, `development_snapshot=true`, `source_dirty=true`, and the Git parent.
The package tool rejects this development evidence. The normal clean-source
generator and its reviewed input pins remain the final candidate boundary.

Build the generated `source/core/scripts/build.ps1` with the existing pinned
LLVM-MinGW `20260826-ucrt-x86_64`, ReShade
`4a50d1eddace85734871d91792ff214f13f66c01`, and bridge
`28aed4099b0fe1c207b20b5fee5364c0773c25c2`. An unchanged-codec cache may be reused
only when its existing compiler/source/options checks accept it. New snapshots
and validation runs require fresh output directories.

The canonical build metadata replacement now initializes the global native
exit-status variable. Its former script-local assignment could hide compiler
failure and continue into linking. An actual failing complete Core source was
compiled with both scripts: the old script continued to a missing-object link;
the corrected script stopped at the failed English C++ compilation. Neither
run produced a successful Core artifact.

## Focused results and limits

The private evidence batch is `dline15-20260912`. Development snapshots r1–r10
contain 326, 333, 334, 336, 338, 340, 340, 341, 341 and 341 input files respectively. Initial compilation
failures are retained; r4 is the first successful complete bilingual Core build.
The r5 build adds SourceFrame transport, baseline FG consumption, native-SR-once
and prefixed feedback fixes. r6 adds the first HUDless implementation; r7 adds
current source retry identity, Present backoff and FG observation/retirement.
r8/r9 add opaque-failure boundaries, FG capture, explicit UI observation origin,
and compatibility F7 controls. Real capture testing exposed incorrect guide
parameter keys; r10 uses the production `DLSSNR.Depth` / `DLSSNR.MVec` keys.

| Check | Observed result |
| --- | --- |
| Actual resource semantic resolver | 119 CPU checks passed; the false-Unique mutation was rejected |
| Actual native format ledger/runtime/hooks | 1,161 host CPU checks passed |
| Actual exposure view/capability adapter | 10,773 CPU checks passed |
| Actual typed exposure shader | 80 R8_UNORM/R32_FLOAT pairs passed on D3D12 WARP; pre/post Encode/Combine, descriptor rebuild/rebind, normalized red and original bytes |
| Default FLOAT/exposure/codec regression | 384 paired/grid scenarios and 504 byte-identical reference outputs passed on WARP; typed CopyCS and motion access also passed |
| New Present shader entry compilation | ConvertCS, InitCS and InitColorCS compiled with D3DCompile for cs_5_0 |
| First full Core compile | Stopped on incomplete multi-pass declarations and an explicitly unlisted new header |
| Second full Core compile | English C++ compilation passed; link stopped on the then-missing source-frame OnBarrier implementation |
| Third full Core compile | Stopped on the then-inconsistent private Present capture request return type |
| Fourth full Core compile | Both language Core artifacts and nrchain compiled and linked; numeric PE `0.5.1.15` verified |
| Fifth full Core compile | Both language Core artifacts and nrchain compiled and linked with baseline SourceFrame/FG consumption; numeric PE `0.5.1.15`; no HUDless path or FG runtime claim |
| Sixth and seventh full Core compiles | Both languages and nrchain passed with the first HUDless implementation, then source retry identity/Present backoff/FG feedback; PE `0.5.1.15`; development evidence only |
| Eighth through tenth full Core compiles | Both languages and nrchain passed with capture/UI changes; r10 contains the corrected production guide keys; real export evidence remains separate from compilation |
| HUD shader compile | Actual Present/HUD string composition passed D3DCompile for HudPS, alongside actual FullscreenVS/BlitPS and the existing compute inventory |
| Full r5 Core native-color WARP | 11,293 assertions and 58 native callbacks passed: typed 28 / TYPELESS 27, pre/post, half/full, identity/edited and Motion rotation; artificial neural planes, no NGX/game |
| Actual three-position UI | Both languages compiled; all three starts, session fallback display, same-button reselection and Full NR 2/3/4 controls exercised |
| Actual ProcessEvaluate suffix and Session | 36 CPU assertions passed, including no-private-work suffix, source/recorded-work exclusions, exactly one native SR even with legacy retry=true, and parameter restoration; two compiled behavioral negative controls rejected |
| Config and persistence | 730 real Win32 INI checks, eight fresh-process restarts and nine compiled behavioral negative controls passed; legacy retry=true is normalized off; exact old 2/3 scale and session-only reselection preserved |
| Screenshot naming | 17,559 checks passed; Chinese/English ASCII output identical and bounded |
| Private F8 coordinator | 1,297 checks passed, including the independent compatibility request and exact original route-session restoration after transient native matrices; six face and three matrix behavioral negative controls rejected |
| Actual feedback serialization and readers | 345 core collector/ZIP and 103 combined TXT/ZIP assertions passed with FG/capture counters; new fields carry begin/end prefixes; prior combined capacity/privacy/unknown-evidence controls retained |
| Actual activity display and receipt | 152 assertions, seven prior and two new compiled negative controls passed; compatibility publishes distinct display states and explicit observation origin without native Window reads; 4,000 concurrent publications without lock violations; bounded F8 export 6,166 bytes |
| Actual compatibility F7 UI | 56 bilingual assertions passed: only current-layer request, no ignored native matrix controls/old results, live pending/exporting/failure/completion, and independent F8 TXT |
| Full NR independent hosts | 108 policy checks, 54 actual-wrapper checks and 705 real WARP spatial-chain assertions passed; neural model is an explicit shader substitute |
| Opt-in real NVIDIA Present host on r4 | Official SDK bootstrap and signed Feature18 passed 1/2/3/4 full-size layers and two layers at 50%, using artificial 640×360 input; 1,351 assertions across five cases |

The format/exposure and full-spatial WARP checks use actual extracted production
code and artificial neural output. The separate `present-ngx-r1` host executes
real NVIDIA NR against r4 but uses artificial input; it does not establish a game
result, FG consumer behavior, display cadence, frame time, or visual quality.
The whole-Core native-format WARP host passed on r5; final frozen-source
acceptance remains separate from this development snapshot.

The r4 input manifest SHA-256 is
`414d0c2d4b0586a07b1b47e3db2f0e726817f3ab4a8c8480532a4283004e1159`;
its generated source manifest is
`5ec1427118d25e6e30ac5ff8bf9b5e827b0a4d833ffbdd27114d1e12097a99d7`.
The English Core SHA-256 is
`9fcba0e3a0553b54d72c760557fa8b2ec567169dccbb4e0ba03777b2447a4b4d`;
the Chinese Core is
`6a6e77d54458778b892fabac374acaafde573abdc61cccd565e89f08979ab540`.
These are development artifacts, not a frozen-release acceptance receipt.

The r5 input manifest SHA-256 is
`2ac8298e434bfe61e5069edabe969d5c207315eaef76c84b1f09e518c09a0ef9`;
its generated source manifest is
`fb52d8167cd27b61204c7870b7f31cd30fd2395791edc4395187d8e769ed0c51`.
The English Core is
`9336a0ef03a86057b8f3ebd6bca875d0606d31e4205130adcc7bfa8d06ec04de`;
the Chinese Core is
`fe8fdc2e7b21e1b40507d88750456248a6de43c609bd6062a2fe302140a4f5f0`.

The r7 generated source manifest SHA-256 is
`7882dd3127b31cfc7e865b3ce9d3ece70af370a7e000fc6e02c3062836cb30ff`;
its input manifest is
`258f42686c4335e8120f61e1fff234306a2d6a5b93f51d9e64c0cbd2d19ff0ae`.
The English Core is
`34b3a872918849306d3ca376b2327eb827ca8e19f9b5c0b081bd80653c1ed5cd`;
the Chinese Core is
`a756e1c32d426042977f63d4a289254ffc47d6d84f0bd29895d9f10649af3c49`.
Each development build receipt retains its actual command, parent, source hashes,
PE identity and binary hashes. The D15 package reads its own language-specific
installation guide and records full-size 2/3/4 pass support; historical D11/D14
guides are not shipped as current instructions.

## No-DLSS cold initialization (r11)

The old `EnsureNrApi` required the host to have loaded `_nvngx.dll` and would
permanently bypass if it was absent. The r11 bridge statically links the official
NVIDIA NGX SDK loader and calls `NVSDK_NGX_D3D12_Init_with_ProjectID` when the
host core is absent. Parameters still come from the real driver core. This
creates no Feature1 and requires no game NGX Init call. Failed cold Init retries
after five seconds, even if failure loaded the DLL. Loaded bridges are reused.

The bridge is built with MSVC `/MT`, NVIDIA/DLSS commit
`a291cc7d2cc642a51566f3dfd5376f635cd1b284`, API macro `0x15`.
The `nvsdk_ngx_s.lib` SHA-256 is
`36eab29264c2a06456ba8415f20086aea36f0cbb314d020085cb288cc06bb9ae`.
Three header hashes and the SDK license hash are checked in
`scripts/build-nrchain.ps1`. The source repository contains no SDK headers or
library. Packages include the SDK license and identify the embedded loader.
The bridge imports only Kernel32, User32, Advapi32 and Shell32, with no new VC
runtime dependency. The signed NR runtime remains separately supplied.

Full builds now require `-NgxSdkRoot <official DLSS checkout>` and
`-VcVarsPath <vcvarsall.bat>`, or `DLSS5_NGX_SDK_ROOT` and
`DLSS5_VCVARS_PATH`. `nrchain-build.json` records exact inputs, the command-file
hash and output hash. There is no custom registry discovery or bare DLL search.

`validate-nrchain-cold.py` passed real NVIDIA cold initialization and driver
parameter Allocate/Set/Get/Destroy. The official loader selected the DriverStore
core; Windows reported a valid hardware-publisher signature.
`validate-nr-api-cold-policy.py` passed 27 checks against actual `EnsureNrApi`,
plus two compiled negative controls. Its OS/SDK boundary substitutes establish
policy only. The real TXT/ZIP feedback collectors also passed 345 and 103 checks.

The full r11 Core passed `validate-compat-present-ngx.py --cold-bootstrap --case all`:
five fresh-process scenarios (one through four full-size passes and two at 50%),
1,558 checks, real signed Feature18 evaluations, four adopted output frames per
scenario, actual pixel readback and F6 stop. No host bootstrap DLL was present.
Inputs are artificial; this does not establish a game's renderer or ReShade
registration path. The separate no-DLSS production-callback host covers that boundary.

r11 contains 344 frozen inputs. English Core SHA-256 is
`d55a8ccafba869984a853ec27e9772890d5a65f6996d0286738f675c60977ab0`,
Chinese Core is
`c9cc305b8b70437b1eac6eb5de530df1d218604476e910ba1df59191eefcc0d9`,
and the 222,720-byte SDK-enabled bridge is
`1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2`.
These remain development artifacts; final clean-source acceptance is separate.

The actual ReShade cold host exposed two additional integration boundaries:
the r10 small-struct `get_back_buffer` return ABI wrote through an invalid return
pointer; the production code now uses native COM `GetBuffer` with an out pointer.
In r11 a resource returned a proxy device, which created proxy command lists
subsequently submitted to a raw queue; production now creates private resources
from the paired API-native device and queue. Neither failure was an NR model or
algorithm result. Both negative runs remain preserved. Packaging requires an
actual successful `validate-nodlss-activation.py --case all` on the exact final
source manifest and bridge, including one/four-layer real readbacks and source
stability. Pass its private `validation.json` via `--activation-evidence`.
Old internal NGX, CPU or WARP passes cannot satisfy this gate.

## Issue-specific acceptance

- **#253**: D14 already added bounded-LDR typed R8 output handling. D15 retains
  that shared path; a D13 rejection is not a new D15 failure. Verify selected
  view, declared LDR/exposure contract, current NR and writeback growth on the
  eventual same-build game report. **#247** remains the older-version control.
- **#202 / #248**: use the existing current-lifetime semantic/recovery result.
  Do not force TYPELESS to UNORM or infer the view from the available codec.
  Format recovery, exposure, queue and whole-frame performance remain distinct.
- **#228 / #229**: reuse the already implemented typed R8 normalized-red reader;
  no new exposure whitelist was added. R8 TYPELESS, UINT and SNORM remain
  rejected. **#68** is only a comparison until its current direct gate is proven.

The complete D15 gate additionally requires two-to-four-pass policy and actual
wrapper ownership/failure checks, new source-frame and FG contracts, actual
Present conversion/copyback, full descriptor/history retirement, both language
Core artifacts, and final source/PE/hash verification. Game validation and real
NVIDIA validation of the final source remain separate from development-host evidence.
