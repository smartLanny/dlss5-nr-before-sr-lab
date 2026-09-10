# Manager RTX20/30 external MFG thin integration — #235

Independent of Core/RR performance #234 / PR #236. Parent is current GitHub Manager #195, `29ccaeb5d63dc53ec9bb40bb6daf50a1fa4fa86d`. The owner's newer 048 local UI branch was not available as a matching remote in this execution. This commit therefore adds isolated modules and an exact, fail-closed call-site integrator; it does NOT overwrite historical complete UI files or enable this feature merely by merging the commit.

## Local integration (required)

Cherry-pick this single Manager commit into the CURRENT local Manager worktree, not Core/main. Preserve other agents' changes. From its root:

```powershell
node --test test/sm75-sm86-mfg.test.js test/sm75-sm86-host-integration.test.js
node scripts/integrate-sm75-sm86-mfg.js --check
node scripts/integrate-sm75-sm86-mfg.js --apply
git diff --check
git diff -- src/product/fg-components.js src/product/launch-coordinator.js src/renderer/launch-settings-ui.js src/renderer/index.html main.js
```

The script validates every exact seam and JS syntax BEFORE any write, keeps CRLF/BOM, is idempotent, and refuses an ambiguous/changed/partial local seam. It patches only five host files: decorate the existing FG factory, show a separate component panel, carry explicit cap settings through the current IPC, load the UI module, and include the new receipt in launch readiness / missing-EXE / rebind protection. It never runs a game or changes user/game files. If the newer local UI moved these functions, port the small `edits` table manually; do not replace the whole local file with #195. Run the full current Manager tests and a real Electron interaction check after integration. Stage the five resulting host changes as the local integration commit.

## What is implemented

- A separate `createSm75Sm86Components` service, wrapped around existing 40/50 components; no NR/SR/driver writer, new Core FG hook, inference engine, or Electron graphics-DLL loading.
- Exact RTX20 -> SM75 / RTX30 -> SM86. Reject unknown/mixed or multiple detected discrete GPUs. The installation-family alias RTX40 is not used as FG capability. The present detector does not prove the actual render adapter LUID; ambiguous systems remain blocked.
- Explicit x64 PE EXE, D3D12 and existing Streamline DLSS FG evidence. First recipe is Native 0.2.4, PTX / exact sampling by default. Service allows Auto; Cubin remains blocked without physical-SM proof. SM75 approximate sampling is rejected.
- Five public INI settings. UI exposes generated-frame CAP 1/2/3 (highest total 2x/3x/4x), explicitly applied while closed and effective after restart. This is NOT live multiplier control, and a game's 2x-only request is not converted to 4x by a cap alone.
- External manifest SHA256 and x64 PE/DLL checks; exact upstream proxy variants, no blind renaming. One free proxy and one managed INI beside the selected render EXE. Never replace native nvngx_dlssg.dll. No default install or download.
- Shared production file-journal transaction/capture, guards, anti-cheat confirmation, symlink/hardlink refusal, target/EXE/manifest/source revalidation, per-game mutation exclusion. Unknown existing INI and other FG/Opti owners are blocked; other mods' occupied slots are not overwritten. These bounded signatures are not exhaustive detection of every renamed mod.
- Own receipt `_DLSS5_Backup/xiaofeng-sm75-sm86-mfg.json`. Recovery needs neither the resource bundle nor current GPU; it deletes only exact owned files. External edits are preserved/refused. A file appearing after journal capture retains pending recovery rather than being silently erased.
- UI/IPC seam tests preserve the selected cap across busy render and anti-cheat retry. Startup validates installed native components even when no legacy FG request was saved. `ready` means files/configuration only; `runtimeVerified` and `generatedFramesVerified` remain false.

## External runtime: not bundled / must be prepared separately

Use the original Native 0.2.4 package from `sdli1995/dlssg_for_sm86` after verifying upstream provenance/checksums and applicable license. No NVIDIA model/runtime, proxy binary, or invented publisher hash is committed here. A user-supplied hash proves correspondence with that manifest, not independent authenticity; the maintainer must approve the source.

The service uses explicit `sm75Sm86Resources` when supplied, otherwise packaged `resourcesPath/fg-sm75-sm86` with a manifest, otherwise `appDir/resources/fg-sm75-sm86`. The first version supplies no new directory-picker/download flow. Place the verified assets and this maintainer-reviewed schema in that directory as `manifest.json`:

```json
{
  "schemaVersion": 1,
  "backend": "dlssg-native-sm75-sm86",
  "runtimeVersion": "0.2.4",
  "upstream": "sdli1995/dlssg_for_sm86",
  "sourceCommit": "REPLACE_WITH_REVIEWED_40_HEX_COMMIT",
  "proxies": {
    "version.dll": {"file": "version.dll", "sha256": "REPLACE_WITH_VERIFIED_64_HEX_SHA256"},
    "winmm.dll": {"file": "altnative/winmm.dll", "sha256": "REPLACE_WITH_VERIFIED_64_HEX_SHA256"}
  }
}
```

The example is intentionally invalid until real values replace placeholders. Include only actually verified entries. `altnative` is upstream's directory spelling. Allowed alternatives are winmm/dinput8/winhttp/dxgi, each its own upstream binary; arbitrary cross-proxy renaming is forbidden. Source bundle upgrades require restore first, not silent replacement. Existing RTX40 receipt must be restored using the old service before migration; automatic ownership migration is not implemented.

## Executed checks and remaining acceptance

Node 22.16.0 on Linux: **49 passed / 0 failed / 0 skipped**. 36 service tests use actual temporary directories and the repository's unchanged production WAL/safety helpers; PE/hardware/FG evidence is constructed, no DLL is executed. 13 host/UI tests execute the renderer helper and patched minimal call-site harness, with dry-run/apply/idempotence, CRLF/BOM, altered-seam zero-write checks, cap/IPC preservation and standalone startup readiness. They are not a complete real Electron application test.

Production dependency copies used locally matched repository blobs: file-journal `927b7ddbb9e704daa0d680dd6703be07167d88ae`; launch-safety `abf9166a6eb76b671f48a2d24a909452d4b5a808`. Those unchanged dependencies are not re-committed. Source/script syntax checks passed. Only JS/test/Markdown added; no payload, private path, credential, raw log, binary or automatic CI workflow.

ROG plugin unavailable in this turn. Windows build/package, full Manager suite, real Electron layout, selected-proxy loading, physical RTX20/30, NR+MFG coexistence, generated output and display cadence remain UNVERIFIED. No runtime package has been downloaded/installed, no installed Manager changed, no release/main merge. Keep #235 open. First game acceptance should use existing D3D12 DLSS FG, then compare FG off/on with NR off/on and record base frames separately from generated/displayed frames. Missing dependencies/failed routing must show feedback instructions, never a false active badge.
