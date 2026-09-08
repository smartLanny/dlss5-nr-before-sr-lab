# Third-party notices

## DLSS5-Swapper

Parts of the game-library discovery, PE inspection, game scanning, process
guards and file-journal foundation are adapted from DLSS5-Swapper:

- Project: https://github.com/rakanki911/DLSS5-Swapper
- Original author: Rakan Alkhaldi
- License: MIT
- Baseline commit: `ccb67f4bc92cb5da1d25416dd6b338e12fa432c5`

The original copyright and MIT permission notice are retained in `LICENSE`.

The three pure INI helpers in `src/product/launch-ini.js` are adapted from
DLSS5-Swapper commit `027d1becef8d048b757eb76ced639df583374917`; their full MIT
notice is included in `src/product/SWAPPER-INI-LICENSE.txt`.

## Launch configuration controls

The scoped SR/FG configuration compiler and NVIDIA profile adapter were migrated
from this project's frozen public Manager implementation at
`0b386eb2de1cc392cf69a8b6fb809800f9e1cc62`. Public configuration contracts are
documented in the compiler's `SOURCES` object. This code manages existing
OptiScaler/RTX40 control configuration and public NVIDIA profile settings;
The configuration code itself does not implement rendering or frame generation.

## RTX 40 frame-generation compatibility components

Release resources include the original MIT-licensed components from
[RTX40MFG-Unlock v1.2](https://github.com/dashdogy/RTX40MFG-Unlock/releases/tag/v1.2):
RTX40MFGCore.dll, RTX40MFG.asi and RTX40MFG-UI.addon64. The full copyright and
permission notice and MinHook license are retained in `resources/fg-components/LICENSE`
and `resources/fg-components/MINHOOK-LICENSE.txt` and included in packaged resources.

[Ultimate ASI Loader v9.7.4](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v9.7.4)
provides the x64 early-load proxy. Its full MIT license is retained as
`resources/fg-components/UAL-LICENSE`. Fixed source archive URLs and SHA-256 hashes
are recorded in `resources/fg-components/manifest.json`.

These components require a game that already provides compatible Streamline DLSS
Frame Generation. The manager does not distribute or replace NVIDIA or Streamline
frame-generation DLLs as part of this compatibility package.

## Lucide navigation icons

Four navigation icons are copied without path changes from Lucide `0.468.0`
(`f12b0de177fbc2a6795e99be065887e72b237123`). The full ISC/MIT attribution is
included in `src/renderer/icons/LICENSE`. Source: https://github.com/lucide-icons/lucide.

## ReShade runtime

The release payload may contain an official ReShade add-on-enabled x64 runtime.
ReShade remains separately licensed by its authors. It is not committed to this
repository and must be prepared from a lawful official source.

## NVIDIA runtime

`nvngx_dlssnr.dll` is not part of this repository and is not licensed by the
MIT license above. The project does not download, commit or grant rights to any
NVIDIA binary, model or SDK component. A release maintainer must supply runtime
files only where they have a lawful right to do so.

## NR-before-SR runtime

The manager treats the NR-before-SR add-on and bridge as external payload files.
Their source, binaries and licensing remain separate from the manager unless a
future release explicitly states otherwise.
