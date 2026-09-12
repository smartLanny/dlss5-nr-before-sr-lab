# Legacy Feeder external-provider adapter

This directory preserves the source transformation and build entry points used
to connect the official DLSS5-Feeder 0.15.1 transport to
`NRExternalProviderV1`. It keeps the existing x86 host, D3D10 relay and
D3D9On12 paths; the project Core, `nrchain_nvngx.dll`, Core configuration and
NVIDIA NR runtime are supplied separately by the Manager.

`stage.py` accepts only the locked upstream Feeder commit and creates a new
generated source tree. It copies the current Core-owned V1 headers supplied by
`--interop`, extends the existing IPC v9 exchange with exact frame completion,
and removes the old private Feature1 consumer. The helper sources in
`feeder-beta3/` are the production adapter inputs needed by that transformation
and by the D3D9On12 provider build.

Use `build.ps1` for the x64 provider/host, x86 provider and x64 D3D10 relay.
Use `build-d3d9.ps1` for the x86 and x64 D3D9On12 providers. Both scripts build
into a new output directory. Compilation does not claim GPU completion, game
compatibility, HDR correctness or visual-quality acceptance.

The upstream Feeder is MIT licensed. Retain its `LICENSE` in any distributed
component package. ReShade/VORT and other transport dependencies keep their own
licenses and are inventory files rather than Core payloads.
