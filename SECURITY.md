# Security Policy

## Scope

Security-relevant reports include device removal, GPU hangs, use-after-free,
incorrect D3D12 resource-state transitions, out-of-bounds subrect access,
unrestored NGX parameters, unsafe hook teardown, and accidental collection or
publication of sensitive local data.

Visual-quality disagreement and unsupported Feature 18 result codes are usually
compatibility issues rather than security vulnerabilities, unless they lead to a
crash, memory corruption, or persistent modification of another component.

## Reporting

Use a private GitHub security advisory when available. Otherwise contact the
repository owner privately before filing a public issue. Include only:

- a concise reproduction sequence;
- GPU generation, driver version, API, render/output dimensions, formats, flags,
  and sanitized result codes;
- the smallest source revision or commit that reproduces the problem;
- a redacted log excerpt with paths and pointer values removed.

Do not upload vendor DLLs, modified runtimes, game files, model payloads, raw
process dumps, access tokens, or full unredacted logs.

Official release archives are published only from the Bilibili profile at
https://space.bilibili.com/941799 and contain an ECDSA-signed release manifest.
Run the bundled `Verify-ReleaseManifest.ps1` after extraction. The project
signature detects modified package contents but is not a commercial Windows
Authenticode certificate and does not establish trust in a third-party runtime.

## Operational warning

This project hooks graphics calls inside another process. Use it only with
software that explicitly permits ReShade add-ons. The project does not support
anti-cheat-protected or competitive multiplayer environments and does not attempt
to bypass integrity or signature checks.
