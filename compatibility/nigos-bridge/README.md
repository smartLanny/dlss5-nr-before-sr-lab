# NIGos D3D11 bridge compatibility source

`source-lock.json` pins the official NIGos `v1.4.13-pre7` source and the small
production-source compatibility delta maintained with this Core. Apply
`nr-transport.patch` to that exact commit with `git apply`, then follow the
upstream build instructions and retain its MIT license.

The delta keeps upstream active rectangles and normal GPU queue ordering. It
adds an explicit delivery contract, same-frame fallback when the bridge declines
before recording, and adaptive output-completion handling for the transport's
teardown boundary. It does not import Core, NR runtime, Manager, local paths,
binaries, or validation claims into the bridge source.

This lock records source identity only. A compiled bridge remains a separately
versioned compatibility component and requires its own basic load/transport
acceptance before Manager marks that binary runtime-verified.
