# NGX layer lifecycle and NR ownership

Implementation source: `dd9a06ad0fe6ed88afc1b5e1c9afa6c1e94de01b`, based on
the isolated output.2 candidate `5abb152`. Product version/defaults, shaders,
post-output tracing, Manager and deployment are unchanged by this slice.

## Evidence and claim boundary

[#223](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/223) now has an
RTX 5090 Laptop reproduction as well as the earlier GTX 750 Ti sample. Both
report the same DLL hash and `6a0ad76` with `sourceDirty=1`, not output.2.
The owner F8 shows NR runtime not attempted and no NR/carrier/submission success;
its later rescan has one successful observed Feature1 Create, covered core
hooks and an `unknown-evaluate` latch. The bounded report does not identify the
unknown handle's actual Create. A late/pre-observer Create is not established
merely by the previous diagnostic wording.

The source did discard every nested Create result and exclude every Feature18.
This candidate repairs those observable lifecycle gaps. It does not yet prove
the SOTTR cause or a game fix. [#107](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/107)
also remains broader than this slice: cooperative external-frame provider
transaction isolation is not the same as the native Feature18 registry here.

## Contract

- Each hooked layer retains its successful Create result, feature identity and
  generation. The same raw pointer in two independent layers/providers is not
  sufficient to merge identities. A unique nested Feature1/18 child can share
  its parent's context only with the same live non-null command list, parameter
  object, observed create dimensions/flags/quality, owner and unchanged even
  observer epoch. Missing declarations, different lists, different features or
  ambiguous siblings remain separate records. RR records remain separate.
- An explicit private bridge scope establishes the origin of our synchronous
  calls. Internal private calls may use a different list/parameter object;
  private origin and proof that two handles alias are separate decisions.
  A later private Evaluate/Release origin requires a known private bridge
  handle. Feature18 outside a proven private origin is external or uncertain,
  never classified by its DLL filename alone.
- External Feature18 retains ownership from successful Create through observed
  Release, even while not evaluating. It uses the existing nonblocking ownership
  gate and invalidates private history. Nested/busy changes preserve the native
  result and abort/quarantine active modifications. Failure, pending creation,
  partial alias release, identity collision and capacity overflow remain closed.
- Release queues a bounded `(canonical handle, generation)` retirement request
  without acquiring the chain lock. The existing collector consumes requests
  under that lock and retires only matching generations. Actual GPU completion
  and chain-external Feature18 destruction remain required. A failed Release
  retains its owner and dependent resources.
- The registry lock only protects values. It never calls NGX/COM, waits for GPU
  completion, acquires the chain lock or performs destruction. Rendering keeps
  the order ownership gate → chain → NR API → short registry operations. Module
  publication keeps its existing rescan → evaluate-hook ordering.
- F8 has 16 recent lifecycle events and an immutable first-fault window of at
  most 16 events. Layer, owner, call/parent tokens, generation, epoch and result
  are anonymous process-local observations, without exported pointers. An
  unknown lookup says that lifecycle proof is absent; it does not assert when
  the handle was created.

Unknown/observer-gap protection is unchanged in purpose. No unknown handle is
promoted to Feature1 and no guard is converted to post NR or a Present provider.
[#226](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/226) remains an
independent policy task.

## Local verification

`validate-ngx-lifecycle.py` froze the five tested input files and recorded a
clean `dd9a06a` identity. Its actual production Create/Evaluate/Release hooks,
registry, ownership gate, retirement collector and F8 writer pass **91 checks**
with real Win32 locks and an opaque fake native runtime. The rendering admission
prefix is production code; GPU work beyond that prefix is a fixture seam.
The test neither replaces lifecycle hooks with no-ops nor claims to execute
real MinHook detours or a NVIDIA model.

The checks include direct core handles after wrapper Create, one nested
Evaluate admission, differing/identical pointer values, ambiguous siblings,
failed outer/successful inner creation, partial release, multiple external
owners, private origin, wrong private-handle provenance, a busy native Release
while another thread owns both gate and chain, delayed retirement and address
reuse. Two independent negative controls fail at their intended oracle:
discarding inner Create and removing the unknown-handle guard.

The unchanged committed post adapter also accepted every exact anchor on the
frozen source. Its focused Release extraction passes **54** result/nesting/
retirement scenarios; removing RR retirement fails its specific negative
control. This is a focused adapter test, not complete candidate staging or a
replacement for the integration source pins.

Related local gates passed: 38 production forwarding assertions, six RR route
tests, two compatibility-route tests, existing observer publication/epoch
scenarios, generic RenoDX's real LoadLibrary policy test, and the modern core
feedback collector/ZIP privacy checks.

The legacy feedback-capture fixture still fails to compile because it lacks
RequestCore, FeatureRecord.feature_id and GetUInt seams. The same failures were
reproduced with the exact parent `5abb152` source/script/template; this gate is
not counted as passed. Base source syntax-only also requires the established
staging transformations for RR depth/UI symbols, so it is not a complete DLL
build result.

Full integrated source-pin validation, DLL builds, WARP/NVIDIA, actual detours
and game validation remain the coordinator's subsequent candidate gates. No
game process, deployment, push, PR or issue mutation occurred in this slice.
The first SOTTR run must still establish the unknown handle's lineage and
compare actual carrier/NR/submission activity and F6 output on the new candidate.
