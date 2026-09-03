# beta0.3.1 startup-stability hotfix

Product: **DLSS 5 AI Rendering Upscaling Edition**

Author: **@野生的装机宅 Bilibili**

Only official release location: https://space.bilibili.com/941799

## Fixed in this hotfix

- Fixed the confirmed Cyberpunk 2077 startup crash (`0xC00000FD`, stack
  overflow). beta0.3 allocated a 32,768-wide-character filename buffer directly
  in `DllMain`, giving every DLL notification a fixed stack frame of roughly
  66 KiB. Games can create worker threads with a 64 KiB stack, and thread-local
  state means thread notifications cannot be assumed disabled. The buffer now
  follows the existing `MAX_PATH` sibling-file contract, keeping `DllMain`
  small. The `燕云十六声` log stops at the same pre-Feature-1 phase, so this fix
  directly addresses its strongest startup-crash candidate; a new community
  run is still required for confirmation.
- Removed the beta0.3 startup-time installation of a process-wide native
  `ExecuteCommandLists` observer on every D3D12 graphics queue. Although it was
  not the confirmed 2077 stack-overflow source, it needlessly widened the
  bootstrap hook surface and added baseline overhead.
- Queue affinity is now learned from ReShade's pre-submit event only after a
  real Feature 1 evaluation. The first DLSS frame may remain native while the
  contract warms up; image safety takes priority over forcing Feature 18 on the
  first frame.
- The exact-fence native queue hook is installed only when the submitted
  command list already owns recorded Feature 18 work. Once installed, it has a
  zero-work atomic fast path whenever no lifecycle token is active.
- ReShade queue/command-list lifecycle callbacks now return through atomic
  startup/F6-off fast paths before unwrapping native D3D12 objects. Present does
  not touch NR lifecycle tables until Feature 1 has actually been observed.
- Add-on filename, Windows resource metadata, overlay, startup log and exported
  identity now report beta0.3.1 and the official Bilibili release address.
- Release packages now carry a signed ECDSA manifest and offline verifier.

## Compatibility status

- `燕云十六声` beta0.2 did not crash: it safely bypassed because Color and
  Output were `R8G8B8A8_UNORM` and their linear/sRGB contract was unknown.
  beta0.3.1 does not guess that encoding. A valid per-engine contract is still
  required before its NR color path can be enabled.
- The reported beta0.3 `燕云十六声` failure happened before NGX module/Feature
  discovery, so it is distinct from the earlier R8 no-effect report.
- RR remains a strict safety bypass. This hotfix does not introduce an
  experimental RR+NR combined path.

## Release gate

The exact rebuilt Chinese binary with SHA256
`6460C8E367E586B648E15426B4C6C57C87BAFBC154BF67F88357392FDA706307`
passed the local Cyberpunk 2077 startup and Feature 1 smoke gate on 2026-09-03.
At `1920x1200 -> 3840x2400`, the run remained alive beyond 2,400 carrier
evaluations and recorded 2,388 successful Feature 18 evaluations. Transfer
prepared/recorded/bound/submitted counters remained aligned and the log
contained no device removal, lifecycle uncertainty, duplicate Feature 18,
runtime rejection, or slot exhaustion.

This remains a candidate rather than a broadly qualified release: physical F6,
RR-on, mode switching and DLSS-preset cycles were not repeated on this exact
hotfix binary. `燕云十六声` requires a new community log because the game is not
installed on the local validation machine. The wider Hogwarts Legacy and
Witcher 3 matrix also remains open.
