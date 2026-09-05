# Release signature identity

Official releases are published only by **@野生的装机宅 Bilibili** at:

https://space.bilibili.com/941799

Release archives contain `RELEASE_MANIFEST.json`, its detached ECDSA P-256
signature in `RELEASE_MANIFEST.sig`, the public verification key, and
`Verify-ReleaseManifest.ps1`. The signing key is protected with Windows DPAPI
for the release account and is never stored in this repository or a package.

This project signature proves that package contents match a manifest signed by
the project release key. It is not a substitute for a commercial Authenticode
certificate and does not make third-party runtimes trusted.
