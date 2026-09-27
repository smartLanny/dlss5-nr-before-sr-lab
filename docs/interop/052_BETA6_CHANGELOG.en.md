# 0.5.2 Beta 6: changes since 0.5 D21

This is a candidate note for maintainer testing. The frozen delivery manifest will identify the final source, Core, nrchain, bilingual ZIPs and validation scope. This note does not claim that the game failure has been accepted as fixed.

## Failure addressed in Beta 6

In the Beta 5 one-click comparison inside the actual game, the first three groups each saved three screenshots. The game then crashed during the first private Feature 1 creation in group 4, “post-route NR 50% + private SR.” A controlled ReShade + NGX test on the same game setup found that creation succeeds with a ReShade proxy command list and crashes in D3D12Core with the original command list. Descriptor heaps created through the ReShade device belong to its proxy object domain; the original list requires original heap interfaces.

Beta 6 limits its correction to descriptor-heap binding **inside private NGX calls**. It passes the original heap behind a recognized ReShade proxy, leaves an already-original heap intact, and releases temporary references after forwarding and state recording. It does not change the game's NGX initialization, command list, runtime libraries or saved defaults. Software and controlled NGX checks still need a maintainer game test of group 4 and later groups; this note does not claim the crash, image quality or performance is fixed in game.

## Features carried forward since D21

- **Controls and configuration.** Picture, lighting, experiments, developer and feedback pages show operating state. AI colour range is one slider, defaulting to 0.5 for new settings. F6/F7/F8 can be rebound; unavailable routes explain why.
- **One to five NR layers.** Layers retain separate model settings while the chain applies the working scale once. Eligible post-SR and RR/compatibility routes can work at 25%. Idle CPU, cache, switching and retirement improvements do not establish an in-game FPS gain.
- **Optional temporal and private SR.** With proven source and phase, native motion vectors or AMD optical flow can drive ordinary accumulation or low-frequency reconstruction. After native SR/RR succeeds, private residual SR can reconstruct the final NR edit once. Both features default off and are mutually exclusive. Private SR does not support Present, FG or multi-GPU.
- **One-click Experiments comparison.** The existing nine-group sequence changes temporary settings, warms up and saves up to three screenshots per group at a fixed camera. Completion or cancellation restores plugin settings without writing the INI. Game SR/RR, HDR and FG are not switched. The two-choice viewer opens only when clicked. Images come from different display times, are not paired same-frame pixels and are not performance measurements. Beta 6 **does not add a run-one-group button**.

## Upgrade and validation limits

Keep the existing INI, key bindings, jitter and saved options. On first migration from D21 to unified multi-layer settings, the old `NRSecondScaleNumerator/Denominator` auxiliary ratio is archived rather than applied; auxiliary layers use the primary work grid. Check effective layer count, dimensions and workload after upgrading. The optional Core-only OTA contains Core, its matching nrchain, manifest, licences and notes. It does not replace NR/SR/RR/FG runtimes or separate face assets, or change the Manager's default component.

When the maintainer schedules the next game test, rerun the full one-click sequence, focusing on the first private creation in group 4, its screenshot receipt and later groups. Do not count software checks, controlled NGX tests or Beta 5's first three successful groups as a game pass for group 4. Motion artefacts, Esc/map power saving, Cyberpunk 2077 compatibility and the ≤3% added cost of a whole game frame still require acceptance. Controlled 4K temporal timings are multimodal; no stable low-overhead claim is made.

*The source comparison still starts at 0.5 D21 `cbfb33919696a2840ed5154c91abaea56228b661` (historical identifier `beta0.5-dline21-223fix2`). This is a source baseline, not a fresh verification of an old physical OTA; later D21 Perf/unified branches are not the starting point. Keep the Beta 5 game failure and Beta 6 controlled regression under their own identities.*
