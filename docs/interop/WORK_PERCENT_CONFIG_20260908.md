# #82 new-install defaults and rejected work requests

This maintenance continuation starts at the isolated size planner's
`da1a5aefd7b5ca40c2e45ae10d12c32470c3c08a`, whose maintenance ancestor is
#175 `e8e5fa0`. The planner's earlier frozen evidence remains unchanged.
No 0.5 algorithm, Manager change or 1–49% runtime admission is imported.

## User-facing behavior / release note

New installations now start pre-SR NR at **100%, Reference (N=R)**. This
changes only NR's work preference, not the game's render resolution or the
master Enabled default. The supplied INI explicitly stores WorkMode=0 and
CustomWorkScale=1. Existing valid explicit WorkMode/CustomWorkScale values
are retained, including old output-relative Quality/Balanced/Performance/
Ultra presets. Their saved CustomWorkScale is not presented as the preset's
actual N/R. Post-SR keeps its existing 100% default and 50–100% implementation.

An existing INI without work keys remains a legacy configuration: its first
load uses historical Quality / 0.75 defaults for missing keys. Merely lacking
WorkMode does **not** classify it as a new installation. Existing/unknown files
are never reset to Reference by this migration. Later missing keys retain
the session's previous valid work values.

Malformed, nonfinite, truncated or out-of-range front work requests now retain
the previous valid pair and report rejection, instead of silently changing a
below-50% Custom request into 50%. The parser also rejects trailing junk and
floating underflow/overflow. An initial invalid request has no earlier session
value, so it uses the explicitly reported legacy initial pair; it does not
invent a successful model request. The rejected INI text is left intact by
reload. A later explicit normal configuration save may save the effective
valid settings through the existing save path.

With WorkMode=5, exact CustomWorkScale=0 is the explicit master-off control:
NR is disabled, the previous valid precision is kept and written back, and
the zero is consumed. A later F6 re-enable therefore remains enabled. Zero in
an inactive non-Custom field is rejected, not treated as a surprise off action.
The UI's existing 50–100 slider is unchanged; its tooltip explains new versus
retained settings, and a front-route note makes a rejected request visible.

## Configuration mechanism and boundaries

The existing `ReloadConfig` runs strength and Mode migrations before most
work settings are read. These migrations can create the INI. The new
`IsNewWorkConfigFile` therefore checks real file absence **before any such
writes**, and only ERROR_FILE_NOT_FOUND on the first session load proves a
new file. Missing parent, inaccessible or existing files do not. The new
Reference defaults are explicitly persisted in the two existing work keys.
There is no new global ConfigVersion migration that could reinterpret an old
preset. Existing ConfigVersion=4 and independent strength migration remain.

`nr_work_config.h` supplies the narrow parser/validation policy.
`ReadWorkRequest` handles missing versus invalid fields before normalization.
`ApplyConfig` validates the same numeric contract under the existing chain
lock against the latest effective pair; `NormalizeConfig` no longer clamps
work mode/scale. Other fields keep their prior normalization and defaults.
The normal config/history-generation mechanism still owns changes and Reset.
Rejecting the same invalid work request does not change the work pair.

The source and generated TU contain only the two existing assignments to
`g_config`, in ReloadConfig and ApplyConfig; there is no field-level work
write bypass. F8 `GroupConfig` copies its original valid SnapshotConfig:
ordinary comparisons preserve the work pair, and ShareMatrix chooses only
Custom 0.5/0.75/1.0. Both temporary application and restoration still call
ApplyConfig with persistence disabled. Removing work clamps from the helper
therefore does not admit an invalid F8 work pair.

The explicit `RestoreRecommendedPictureSettings` button intentionally keeps
its historical Quality / 0.75 preset and still goes through ApplyOverlayConfig
and ApplyConfig. It is not the new-install default. Its formerly inaccurate
"matches INI" comment is corrected after the 3411 freeze; this comment-only
change is not claimed to be in the frozen DLLs and does not change behavior.

Only a proven-new file or the explicit zero control writes through
`PersistWorkControl`. Concrete writes are checked independently and the first
failure enters the existing persistence-error/session-only latch, retaining
the in-memory setting. As elsewhere in this INI path, a group of key writes
is not a filesystem transaction; partial-write failure is reported and does
not claim successful persistence. No new backup/whole-file replacement flow
is introduced. Existing package policy preserves the user's INI, and this
runtime change does not overwrite valid legacy work keys during load.

## Actual focused validation

Frozen production and test source:
`3411a51433149219ef54b3e88fe242a6f2efc149`.
The generated private stage is `build/work-config-final-generated`.

- 72 focused Windows CPU cases passed using actual Win32 INI reads/writes
  and extracted production helpers. They cover absent/new versus existing
  missing-key files, all six explicit modes and valid saved floats, invalid
  initial/load/reload values, nonfinite/trailing/truncated/underflow values,
  0/off consumption followed by re-enable, and a real write failure with a
  missing parent. The test uses only its temporary directory.
- Source ordering verifies existence is sampled before old migrations and
  numeric requests are checked after the ApplyConfig lock, before equality
  checks. Generated Core C++ frontend compilation passed, with seven existing
  ReShade attribute warnings retained.
- All 116 original generated-manifest file hashes match. The generated Core
  source SHA256 is
  `449f299cf8b6398f37daea7194fa66ca7be6a5d6639123caefd4b5832e74eeff`;
  manifest SHA256 is
  `d21687d5f56b16cb5849d56194d1c8d38f9a1e1f32221be7a349adc10012498b`.
- Private `build/work-config-validation/validation.json` SHA256 is
  `317b1a4703e903f227d2ffc095a2a7e573abb1e88135bcce5ed5f9cbc2ca44ff`.
  CPU log SHA256:
  `b1313471249d61c83994fd94b80f71aad3d938b4339215643473b66a75f57cb5`;
  generated compile log SHA256:
  `afc564792a93384bd22467e0072eaace837c3d8b033683bd654eea05ebc4d2ef`.

The first test-harness compile lacked its standard `<cstdlib>` include; that
failure log is preserved. No production failure was disguised, and the
earlier 38bcbd7 generated stage/68-case check also remains separate. Final
source adds explicit floating-range rejection rather than interpreting
underflow as a zero control.

Reproduce the focused CPU checks with the cached compiler:

```powershell
python scripts/validate-work-config.py --compiler $Compiler
```

At the initial focused-check stage, no Core DLL link had run. The coordinating
task then requested the complete links recorded below. No GPU/NR/SR execution,
hosted CI, game test or publication was performed. UI live/DPI checks and final integrated runtime evidence remain
separate. The unresolved model minimum, input/guide footprint and 1–49%
admission continue to follow the fixed-size experiment proposed in
[the planner handoff](WORK_PERCENT_ADMISSION_20260908.md).

## Subsequent complete bilingual Core / nrchain links

The unmodified `work-config-final-generated/core/scripts/build.ps1` ran once
with the cached llvm-mingw/ReShade toolchains. The script explicitly embeds
source `3411a51433149219ef54b3e88fe242a6f2efc149`, not the later comment/docs
HEAD. All 116 original manifest entries still match after linking. Both Core
binaries contain that exact commit and numeric PE version 0.4.6.1; the inherited
hotfix.1 label is not a new published release or an assertion of old asset
identity. Their actual hashes define this private build.

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| English Core | 1,348,096 | `7f934c873f199d9b76eb6b1cbc3c14509efb1c31683a78c515a357bdddbf1e5e` |
| Chinese Core | 1,371,136 | `4481df0ed01111343cb67f7115b6c2b9999c69766205a02f6d2861df16f81eac` |
| nrchain | 8,192 | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

The unchanged nrchain shim has no Core source/PE version resource. Complete
private log `build/work-config-validation/full-core-build.log` SHA256:
`691e976abb5be85e01476553836a03aa1d8c1c30b344ef07108ce2afb75f633f`.
`full-build-validation.json` SHA256:
`736977762583600ab3ce9ea58fb6534aebb13de32fdf8b443f5be6031689a5cc`.
The earlier minimal-check validation JSON is preserved unchanged and retains
its chronological `dll_linked=false` field. No GPU was requested or used for
these links and no WARP matrix was repeated.
