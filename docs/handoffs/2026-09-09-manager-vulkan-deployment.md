# Manager Vulkan deployment foundation

Date: 2026-09-09  
Scope: deployment state only; no Vulkan route is enabled in `game-support`, no layer recipe or binary is shipped.

## Interface

`createVulkanDeployment(options)` returns four asynchronous operations:

- `inspect({ id, exe })` reads the global receipt, registry value, owned file hashes, per-EXE activation state and pending recovery state.
- `prepare({ id, exe }, recipe)` validates a fixed x64 recipe, launch privilege, registry and activation adapters, then adds one EXE reference.
- `restore({ id, exe })` removes that EXE's Manager-owned activation and removes a Manager-owned layer only after the final reference.
- `recover()` reconciles an interrupted operation to its recorded pre-operation state. If the committed receipt already equals the planned target, it completes the commit instead of undoing it.

The production caller must provide:

- `registry`: `{ identity:{scope:'HKCU',view:'64',key}, read(valueName), write(valueName,expected,desired), list() }`. `write` must compare the exact expected state before mutation and verify the result. The module independently checks before and reads back after every write; this is not an operating-system atomic CAS guarantee.
- `activation`: `{ id, read(exe), write(exe,expected,desired) }`. This adapter owns the exact per-EXE activation mechanism, including its own CAS and durable write. A recipe must name this exact interface.
- `inspectLaunchContext(game)`: returns `{ elevated:false }` only when the eventual game launch is proven non-elevated. `true` and `unknown` are blocked because an elevated Vulkan application does not consume HKCU implicit layers.
- `pe.getBitness(file)`: the current PE reader or a test adapter.

`createWindowsRegistryValues` in `src/product/windows-registry-values.js` supplies the registry interface for HKCU's explicit 64-bit view. It passes key, value name, expected state and desired state as structured JSON over stdin to `windows-registry-values.ps1`; neither field is inserted into a command string. The helper uses .NET `RegistryKey`, distinguishes a missing value from errors, refuses to rewrite non-DWORD values, compares the expected state immediately before changing the exact value, flushes and reads it back. This is best-effort interference detection inside the Manager's process lock, not an operating-system atomic compare-and-swap primitive.

The packaged script path is `<process.resourcesPath>/windows-registry-values.ps1`. Source runs fall back to the script beside the JavaScript module. A packaged run never falls back into `app.asar`; a missing extra resource is reported as `REGISTRY_HELPER_MISSING`. The child process is hidden and has bounded time and output. The adapter has no operation that clears a key or enumerates another hive/view. Both normal and external build configurations retain this extra resource; the external-package verifier rejects it if missing, empty or replaced with a directory.

No registry key is hard-coded. The caller selects the key through `registry.identity`; this permits tests against an isolated non-Vulkan HKCU key without touching `ImplicitLayers`.

## Recipe contract

The recipe is deliberately narrow:

```js
{
  version: 1,
  id: 'fixed-recipe-id',
  release: 'fixed-release-id',
  architecture: 64,
  sourceRoot: 'C:\\absolute\\verified-source',
  layer: {
    manifest: 'ReShade64.json',
    library: 'ReShade64.dll',
    manifestSha256: '<64 lowercase hex>',
    librarySha256: '<64 lowercase hex>',
    name: 'VK_LAYER_reshade'
  },
  activation: { interface: 'reshade-apps-v1' }
}
```

Both `manifest` and `library` are simple leaf filenames in the same directory; subdirectories are rejected. Both source paths must remain inside `sourceRoot`, contain no symbolic or hard links, and match their hashes. The DLL must be x64. The bounded JSON manifest must describe the expected global layer, refer to that sibling DLL without an absolute, parent or subdirectory path, and expose a disable environment entry.

The repository currently contains no production recipe. A future recipe must pin the actual redistributed archive, component hashes and licenses before the route can be enabled.

## State and recovery

State lives under `<userData>/vulkan-deployment`:

- `operation.lock` is an exclusive `wx` process lock. A live owner returns `VULKAN_BUSY`. A dead-PID lock returns `VULKAN_STALE_LOCK` and is deliberately left untouched: Node's file interface cannot atomically prove the inode observed for the PID check is still the inode being removed, so competing recovery processes are not allowed to steal it.
- `pending.json` is written before layer, registry or activation changes.
- `receipt.json` records exact recipe identity, registry identity/state, layer ownership and all EXE/hash/activation references.
- owned layer files live under `layers/<recipe-id>`; temporary restore snapshots live under `recovery/<transaction-id>`.

Malformed receipts, pending records or locks fail closed. EXE path is the unique reference identity; `game.id` is only a display/caller label, so a second label cannot create a second activation reference for the same executable. Missing or changed owned files, registry values, EXE identities and activation tokens stop mutation. A reused external layer is never deleted or rewritten. It is accepted only when exactly one candidate passes the caller's `externalLayer.verify` adapter.

File, registry and activation effects cannot form one operating-system transaction. The write-ahead record, expected-state checks and readback form the recovery protocol. Undoing a failed prepare first disables the EXE, then unregisters, then removes files. Undoing a failed restore first recreates and verifies files, then restores the registry value, then reactivates the EXE. Recovery never overwrites an existing file whose digest differs from the receipt. A mutation failure intentionally leaves `pending.json`; callers must expose `recover()` before another prepare/restore.

## Deliberate limits

- The module supports x64 only. A later x86 design must use and test the 32-bit registry view and matching manifest/DLL independently.
- It does not implement ReShade activation. ReShade 6.8.0.2155 must be integrated from its actual `global_config` contract: the current Vulkan non-proxy check depends on `config.path()` existing and also recognizes its documented install/base-path controls. The historical `ReShadeApps.ini` idea is not assumed to apply. Until a production adapter can prove the exact 6.8.0 per-EXE activation and reversible storage contract, `prepare` is blocked.
- The registry adapter is implemented, but the production recipe, per-EXE activation adapter and proven launch context are still required before enabling a Vulkan route. The module does not lower privileges, write HKLM, launch games, install Feeder, or claim that ReShade/Core loaded.
- Feeder's `VK_LAYER_feed_vk` fallback should remain a per-launch explicit layer once its matching producer/transport recipe is delivered; this module does not register it globally.
- `runtimeVerified` remains false after preparation. Vulkan input → transport → project Core → same-frame output is a separate #164 acceptance step.

## Focused verification

Run:

```powershell
node --test --test-concurrency=1 test/vulkan-deployment.test.js
```

The tests use temporary files and injected registry/activation adapters. They never access Vulkan registry keys, ReShade user values, games, GPUs or drivers. Coverage includes two-game reference counting, duplicate labels for one EXE, last-reference cleanup, external layer preservation, elevated-launch rejection, recipe hash/architecture/path checks, registry and activation failures with recovery, restore ordering, external-file CAS refusal, corrupt state, registry drift, owned-file drift, a live lock conflict, and two independent processes observing one stale lock without taking it over.

Separately, `test/windows-registry-values.test.js` covers structured stdin, packaged helper selection, protocol/timeout/access failures and unsupported value types. Its Windows integration case uses a unique `HKCU\Software\XiaofengManagerTests\<UUID>` key in the 64-bit view, verifies DWORD write/read/removal and interference refusal, and removes its own fixture key. It does not register a Vulkan layer. Registry-adapter and packaging checks therefore must not be presented as Vulkan game compatibility or runtime acceptance.
