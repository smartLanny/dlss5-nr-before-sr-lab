# NR Control Contract v1

This contract is the intended stable boundary between the desktop/in-game UI adapter and the NR→SR rendering add-on. It exists to avoid depending on private RenoDX memory, ImGui table pointers, or version-specific signatures.

## Transport

The transport is deliberately unspecified at v1. A local named pipe is the preferred product path. The rendering add-on must never require Internet access.

Every request and response carries `protocol: 1`.

## Read-only state

`get_state` returns:

- `enabled: boolean`
- `mode: "nr-before-sr" | "nr-after-sr"`
- `intensity: number` (`0.0..2.0`)
- `style: "default" | "natural" | "cinematic"`
- `faceProtection: boolean`
- `detailBlend: number` (`0.0..1.0`)
- `inputWidth`, `inputHeight`
- `outputWidth`, `outputHeight`
- `nrFrames`
- `rrProtected: boolean`
- `shortFuseOwnsNr: boolean`
- `statusCode`
- `statusTextZh`

## Commands

- `set_enabled(boolean)`
- `set_mode("nr-before-sr" | "nr-after-sr")`
- `set_intensity(number)`
- `set_style("default" | "natural" | "cinematic")`
- `set_face_protection(boolean)`
- `set_detail_blend(number)`
- `apply_recommended(profileId)`
- `reload_config()`

Every write must return the authoritative readback state. UI code must not assume a requested value was accepted.

## Safety rules

1. Invalid/unknown commands fail closed and do not mutate rendering state.
2. The existing `nr_before_sr.ini` remains a supported persistence/fallback path.
3. The existing ReShade native UI remains available if the Manager or custom overlay is not running.
4. RR protection and ShortFuse double-pass protection cannot be overridden by this contract.
5. The control channel never distributes or loads proprietary NVIDIA runtime files.
6. No command is allowed to disable the add-on's native safety bypass behavior.

## Beginner-mode mapping

The Chinese Manager should map intent-level controls onto the contract:

- `性能优先` → `mode = nr-before-sr`
- `画质优先` → `mode = nr-after-sr`
- `轻微 / 推荐 / 强烈` → intensity `0.8 / 1.2 / 1.5`
- `人脸保护` → `faceProtection`

Advanced parameters can be added in a backwards-compatible v1 extension, but beginner UI must not expose them by default.
