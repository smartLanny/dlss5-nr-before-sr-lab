# 0.5.2 Beta 14

Beta14 is the continuing development baseline. It retains Beta13's short feature labels, hover details and visible status counters. These diagnostic changes do not change the public DLL ABI, rendering algorithms or saved settings.

- F8 adds bounded Esc/menu transition records to distinguish the main view from separate preview contexts.
- F8 adds a partial ledger of plugin-owned resource allocations. Known allocation sizes, unknown model memory and process-wide usage remain separate; this is not a memory optimization or a complete residency measurement.
- The owner accepted the Esc transition in NTE with native RR enabled. Known allocations returned to their initial value after one layer → two layers → one layer. Other UI/3D preview cases, long-term leaks and other GPUs remain unproven.
- Private SR executed in supported game configurations, but the 61% comparison showed fragmented edges/grain and the owner reported flicker. Quality has not passed. It remains off by default; ordinary reconstruction is the recommended baseline. General denoising and whole-game-frame cost are not established.
- The delivered route is native RR → post NR. Low-resolution NR → one native RR, a second private RR and cross-API transport remain future research.

This OTA contains Core and its matching nrchain only. It does not include NR/SR/RR/FG runtimes or face resources, write INI files or enable experiments. Retain the existing installation, runtimes and rollback package.

The binaries were compiled from `3663affdd77309c3eac9a306849415a1ded4f330`. Baseline main `024f9cc32778c1056db364d76d087ee257da6502` has the same Git tree; the original binaries are not relabeled as builds from main.
