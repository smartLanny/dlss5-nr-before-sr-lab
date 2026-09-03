# RTX 30/40 ShortFuse complete package

The RTX 30/40 package uses the exact same beta0.3.2 add-on and configuration as
the RTX 50 package. Its only difference is the bundled Feature 18 runtime:

```text
File: nvngx_dlssnr.dll
Version: 310.8.SF.0 (ShortFuse SF-v2)
SHA-256: 6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927
```

Extract every file into the active ReShade/RHI add-on directory. Keep the
add-on, `nrchain_nvngx.dll`, `nr_before_sr.ini` and `nvngx_dlssnr.dll` together.
Remove older builds and do not enable another DLSS5/ShortFuse/RHI NR owner.

RTX 30 and RTX 40 use this same package. Start with the default `NR -> SR`
mode. Use `F6` for native-SR comparison and `-` for parameter-labelled
screenshots.

If a game does not engage, the add-on panel and `nr-before-sr.log` report the
live bypass reason, runtime create result, resource format and extents.
