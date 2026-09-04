# DLSSNR.UICorrection parameter contract

Static evidence was checked against the local `nvngx_dlssnr.dll` 310.8.0.0
sample with SHA256
`E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`.
This is a binary-interface finding, not an in-game quality result.

In `PollRuntimeParams`, the `DLSSNR.UICorrection` lookup at virtual address
`0x18001AB81` calls the `NVSDK_NGX_Parameter` vtable entry at offset `0x58`.
The public NGX parameter layout places `Get(const char *, unsigned int *)` at
that offset; the signed `int` getter is the next entry at `0x60`. The same
routine uses `0x60` for `DLSSNR.Style`, which independently confirms the
mapping. If the UICorrection lookup fails, this DLL stores zero.

Therefore the add-on should publish this key with `SetUInt`, not `SetInt`, and
its parameter-binding test should assert the unsigned slot. Changing the new
installation default from zero to one is a separate product decision: this
DLL's missing-parameter fallback is zero. Existing user values must be kept.

This correction only ensures that the runtime reads the requested flag.
OptiScaler's current implementation and notes indicate that UI correction acts
on a separately supplied `UI`/`UIAlpha` layer. The NR-before-SR add-on does not
currently provide that layer, so this evidence does not establish that enabling
the flag repairs subtitles or HUD elements already present in scene color.
