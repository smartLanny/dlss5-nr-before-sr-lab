# Upstream attribution and product boundary

## DLSS5-Swapper

The product direction and several planned integration concepts are informed by:

- DLSS5-Swapper — Rakan Alkhaldi / contributors
- Repository: `rakanki911/DLSS5-Swapper`
- License: MIT

If substantial upstream source is copied or adapted into the future standalone Manager repository, preserve the upstream copyright notice and MIT permission notice in all copies or substantial portions, and retain its third-party notices for components that remain in use.

The prototype in this directory is currently an isolated product/UI skeleton rather than a source copy of the complete upstream application.

## Product identity

Planned downstream product identity:

- Product: `DLSS 5 AI Rendering Manager`
- Edition / maintainer: `野生的装机宅 / Xiaofeng Lab`
- Windows AppUserModelID: `cn.xiaofeng.dlss5-ai-rendering-manager`
- Overlay/profile namespace: `xiaofeng-dlss5-ai-manager`

Do not present the entire upstream Swapper implementation as original Xiaofeng Lab work. In a future downstream fork, About / licenses should clearly distinguish upstream authorship from the edition maintainer and the independently developed NR→SR technology.

## NR→SR core

The NR→SR research core in the parent repository has its own licensing status and must not automatically inherit MIT merely because the Manager may use MIT-licensed upstream code. Keep rendering-core licensing and third-party notices separate until ownership and redistribution rights are finalized.
