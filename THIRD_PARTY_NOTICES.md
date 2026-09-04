# Third-party notices

## DLSS5-Swapper

Parts of the game-library discovery, PE inspection, game scanning, process
guards and file-journal foundation are adapted from DLSS5-Swapper:

- Project: https://github.com/rakanki911/DLSS5-Swapper
- Original author: Rakan Alkhaldi
- License: MIT
- Baseline commit: `ccb67f4bc92cb5da1d25416dd6b338e12fa432c5`

The original copyright and MIT permission notice are retained in `LICENSE`.

## ReShade

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
