# Manager maintenance boundary

The public Manager repository is the only maintained Manager source:
`https://github.com/smartLanny/DLSS5-Manager-ZJZ`.

Historical Manager copies and delivery evidence beside this Core checkout are
frozen inputs. Do not publish, release, or generate a new Manager from them.
Core, Feeder, host, bridge, and runtime components expose versioned manifests;
the public Manager consumes those manifests and owns installation, update,
rollback, profile, and receipt behavior.

This Core repository contains no Manager-specific release workflow to disable.
Its Core build and validation workflows remain unchanged. Historical Core OTA
publishers retain their existing explicit release guards and are outside the
Manager maintenance path.
