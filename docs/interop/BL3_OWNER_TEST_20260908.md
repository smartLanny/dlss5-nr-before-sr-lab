# BL3 owner input / SR test

The owner authorized local installation and will run the game. The installed
diagnostic is SR-only; neither the project's Core NR nor carrier is installed.
The adapter source is `a290233`; source generation was repeated into a fresh
directory and matched the deployed source bytes. This is a local development
install with external shader assets, not a redistributable player package.

The pinned Luma source is `91babfe12abe5f7de0c4890f0efc321d8277887f`.
The matching ReShade runtime was built from its API 20 pin
`18deaa52de0c425a78b329e9cb3c497281cd00ec`, Release x64, full add-on support.
Earlier API 18 Core dependencies were preserved. Luma uses Development-Release
x64, MSVC 14.44.35207, SDK 10.0.26100.0, `UseLumaFSR=false`, packaging and
post-build deployment disabled, and `DISABLE_AUTO_DEBUGGER=1`. The owner adapter
honors `EnableHDR`; the initial local configuration uses FirstBoot=0, EnableHDR=0
and DLSS SR selection. Existing official NVIDIA SR runtime 310.8.0.0 was reused
after verifying its NVIDIA signature. No vendor payload is committed here.

`--owner-test` adds a maximum of 32 return receipts after the existing SR Draw.
`drawSucceeded` is the actual return; `gpuObserved=0` is deliberately retained.
The pre-draw receipt remains separately bounded at 32 rows. CPU tests exercised
5,000 input and 5,000 result calls. The real ReShade/Luma DLL loader probe passed
AddonInit, NAME=Luma, AddonUninit and unload without creating a graphics device
or game. Shader compilation warnings and failed preliminary build logs remain
in private evidence. No BL3 input, NVIDIA evaluation or pixels are yet verified.

| Artifact / evidence | SHA256 |
| --- | --- |
| Patched Luma main | c9db6436e509ce35274adfa20f3292a240f848852167a1525b1c55b7e337a20b |
| Receipt header | 639e33cdba72cfc100ba40f464ea88b1fc288fde52f713578f261e5039dde115 |
| Luma add-on (1,896,960 bytes) | 662d2dfe6e8c707267ca84b068f06adc19afef80597099c9ebf6f8555d2efa64 |
| ReShade runtime (5,586,432 bytes) | 5682b2489a31636f06f89a90db618a88cd377ba92b6298700977c206948bb2bf |
| Luma successful build log | 64d4681a410f47d98f6ee13d6623c300676c03831e343b30a54a5794a865c21f |
| ReShade successful build log | 75c503009a42dd2efec55b41805ca5ace3fdec42a1eef66e70295668beca0990 |
| CPU receipt test | 0360cd413dbd2f3fe89e29edf3e981de993d7404c13e7c414504ac5ffa7875e2 |
| Loader test | d6b1a4684f2896fd4429f328f90e64b53ba0d2fae17ccfcd666f9b9f91ba8c63 |
| Loader result | 99b7816c8ece23e12ead0de856090b852b2573b290f3bf15c5eb897d70d48215 |

The local Steam shortcut requests DX11. The owner should enter a TAA scene or
benchmark for one minute, inspect Luma with Home, then report visible behaviour.
Read the real ReShade log to establish actual API, TAA inputs and SR returns;
the generic adapter remains DLAA/R=O until separately demonstrated otherwise.
The install receipt records four newly added files, exact hashes and original
inventory. Rollback verifies deployed binaries and preserves changed settings
before removing only those additions. The local Chinese guide and rollback
scripts stay with private delivery records. Hosted CI was not run for this slice.

## First owner-started game observation

The first retained real game log confirms D3D11 device creation and API20 Luma
registration, then 32 input receipts and 32 successful SR Draw returns. The
observed shader is `09ee1a63`, PS slots colour/depth/velocity=1/0/2, CB1=4096
bytes, inferred offsets=72/140/127. Render and output are 3840x2160 (DLAA).
Captured declarations include reversed depth, R32G32_FLOAT decoded motion,
MV scale 1/1, automatic exposure and initially changing pixel jitter. Input
`preExposure=0`, `delta=-1` and `drawFrame=0` are recorded fields, not verified
physical exposure, frame duration or frame identities. Early reset becomes
false. This is real resource/SR-call evidence; no Core NR, presented pixels,
visual quality or temporal stability acceptance is claimed.

Private log snapshot SHA256:
`cc00a06073deaf14a0a9613cbff2a17835dcedfcebfe0a904a98d07b391b526d`.
It also contains one irrelevant failed attempt to load the filename `0`:
the local installer incorrectly wrote `LoadFromDllMain=0` as if it were a
boolean; this ReShade key is a filename list. Luma subsequently loads normally.
Remove that optional entry only after the game exits and retain the before/after
INI. Do not rebuild or replace the running add-on for this configuration fix.
