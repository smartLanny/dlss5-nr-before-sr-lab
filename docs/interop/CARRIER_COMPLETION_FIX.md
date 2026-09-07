# Native carrier completion correction

The canonical generator must call `patch-carrier-completion.py`'s `apply(stage)`
after the native carrier and NGX ABI transformations and before manifest hashing.
This changes the DX11 carrier, not the D3D12 core or its admission guards.

The carrier now waits for its actual D3D12 progress fence before it queues the
DX11 output copy. This adds a CPU/GPU synchronization point each delivered frame
and may reduce throughput. No performance parity with the previous asynchronous
carrier is claimed. A frame exceeding 500 ms, failed signal/wait/Close, or device
removal stops this carrier for the process; the original game call remains the
fallback. It does not retry the wait on every frame or signal a completion fence
from the CPU. Creation/retirement waits are bounded at five seconds.

Retirement includes prior DX11 reads/copies and D3D12 writes. Failed completion
keeps allocator fences, textures, parameter references and the NGX feature alive.
Feature release must succeed before its resource dependencies are released.
Quarantine does not recurse through BridgeDisable or attempt later teardown.
Process termination already exits DllMain without vendor/GPU teardown; the pinned
carrier's FreeLibrary branch removes hooks but does not release these mirrors.

NGX initialization, parameter allocation, Create and Evaluate no longer convert
hardware exceptions into ordinary errors. Ordinary returned statuses remain
unchanged; read-only diagnostic probes retain their own exception guards.
The D3D12 device-creation call also propagates hardware faults, while ordinary
HRESULT failure still allows the existing feature-level fallback sequence.

Focused verification uses the actual staged function bodies. Set
`CARRIER_COMPLETION_SOURCE` to the final canonical stage,
`CARRIER_COMPLETION_ALREADY_PATCHED=1`, and `CXX` to the
Windows LLVM-MinGW C++ compiler, then run:

```
python -B -m unittest discover -s tests/native_bridge -p test_carrier_completion.py
```

An older unpatched stage is also accepted when the already-patched flag is unset,
for exercising the transformation before integration. A final stage is tested
directly and must contain exactly one completion helper definition.

The suite executes 35 fake-queue completion/retirement assertions, a real WARP
queue-fence completion/timeout check, and two independent child processes whose
stub Create/Evaluate raise `0xc0000005`. The children suppress Windows error
dialogs; the exception must leave the child with that status, not return a
success/error marker to its caller. It also checks transformed copyback ordering
and the absence of CPU completion signalling. No NVIDIA Feature18 or game
validation follows from these tests.
