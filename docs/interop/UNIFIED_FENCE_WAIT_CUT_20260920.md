# Unified candidate: boundary required before waiting outside the chain lock

Investigated base: `04c860ea2feb1ec509699c95f28f22b0e1693b07`.
This handoff records a source investigation and an implementation cut. It is
not a completed lock-removal change or GPU/performance validation.

## Result

Do not unlock inside the current `PrepareContextUseLocked`. Its two callers
have already resolved borrowed input resources and the outer caller has a
live command-state restoration snapshot. An instance reference alone does not
make those snapshots current when the lock is reacquired.

No supported proxy/native identity defect was found to repair in this batch:

- `OnExecuteCommandList` in `src/nr_before_sr.cpp` uses the actual ReShade
  `command_list->get_native()` and `queue->get_native()` before learning queue
  affinity under `g_chain_cs`.
- `CaptureNativeCommandState` in `src/nr_command_state.inl` reads the attached
  command-state token, looks up its current record, copies state under the
  registry lock and returns `record->native`. Both `PrepareBeforeSr` and
  `PrepareAfterSr` receive that native pointer from `NativeCommandStateScope`.
- `ResolvePostBoundaryListForObservation` in `src/nr_post_boundary_native.inl`
  already validates the attached lifetime interface, token, created/native
  record and device identity. Its bounded alias scope additionally rejects
  changed epoch/reset/device evidence. It supplies observation identity, not
  missing queue history or permission to record NR.
- Official ReShade v6.8.0 confirms that `get_native()` returns `_orig`, and
  D3D12 list Get/SetPrivateData delegates directly to `_orig`:
  [object implementation](https://github.com/crosire/reshade/blob/v6.8.0/source/reshade_api_object_impl.hpp),
  [D3D12 command list](https://github.com/crosire/reshade/blob/v6.8.0/source/d3d12/d3d12_command_list.cpp).
  The local dependency cache has the same delegation. This does not authorize
  guessing another wrapper's native pointer, promoting an alias after reset,
  borrowing another list's queue, or using the sole observed queue as proof.

`docs/interop/DS1_QUEUE_RETIREMENT_20260909.md` already documents genuine
one-shot lists which never acquire two stable submissions. That historical
case explains why verified native identity does not imply queue history;
it is not proof that the current performance report has the same cause.

## Concrete unsafe boundaries in the current call graph

1. `PrepareBeforeSr` and `PrepareAfterSr` call `ResolveNrGuideBinding`, then
   `FindOrCreateContext`, then `PrepareContextUseLocked`. `NrGuideBinding`
   holds raw depth/motion pointers; `ColorContract` holds a raw exposure
   texture; Color/Output and typed-view/semantic contracts are also local.
   `GetResource` returns the parameter getter's pointer without taking an
   ownership reference. A wait ticket must not retain these borrowed locals.
2. `ProcessEvaluate` and `ProcessRrPost` create `NativeCommandStateScope`
   before calling Prepare. Its `Restore()` writes the saved state and then
   copies it into the record found by the current token. It does not compare
   the saved reset serial with the current recording generation first.
   Releasing the chain lock while this restoration snapshot is live would
   create a new obligation; merely rejecting stale admission afterward is
   insufficient if scope destruction still restores old state.
3. `DispatchEvaluate` has already constructed `nr_source_runtime::EvaluateScope`.
   Its constructor may call `CaptureNgxGuides`, which can append actual copy
   or shader commands. Moving the wait only before NativeCommandStateScope
   is therefore not a completely unrecorded boundary. Source claims and
   guide-copy ownership must be considered, not silently discarded/restarted.
4. `CanDestroyContextLocked` has recording/fence/binding/quarantine guards but
   no CPU preparation/wait reservation. `CollectRetiredContextsLocked` copies
   a completed retired context into deferred destruction and assigns `{}` to
   its original slot. UI/feature retirement can progress after the GPU fence
   completes while a waiter is still asleep. Pointer equality is insufficient.
5. `NrSubmittedFenceEvent` is process-lived and retains its registered fence
   after timeout, which is useful. Its arm/reset/notification functions have
   no internal synchronization; the existing chain lock serializes them.
   A stack event closed after timeout is not a safe replacement.

## Minimal complete implementation cut

Implement this as one admission transaction, not an unlock helper in Prepare:

1. **Separate nonrecording admission from recording.** Extract common input
   resolution from Prepare into an owning `NrAdmissionInputs` contract. It
   holds references to the exact native list/device and Color/Output/depth/
   motion/exposure resources needed across an unlocked interval, plus copied
   scalar inputs, dimensions, format/view/descriptor generations, feature
   generation, config/history generation, route and model tuning signature.
   Do not retain a pointer into the caller's mutable parameter block as the
   snapshot. No private command or parameter substitution occurs in this phase.
2. **Put the wait before source-guide recording and restoration capture.**
   Split `EvaluateScope` identity/policy setup from its `CaptureNgxGuides`
   action, and defer that action until admission resumes. Arrange the common
   outer dispatch so `ProcessEvaluate` and `ProcessRrPost` use the same owned
   admission transaction before constructing `NativeCommandStateScope`.
   Pass the actual ownership-gate transaction through; do not acquire a second
   gate and accidentally treat the legitimate call as nested. Preserve source
   sequence/claim semantics and exactly one native Evaluate on every decline.
3. **Reserve a real context occupancy.** Add a CPU preparation reservation
   and an allocation generation assigned when a `g_contexts` slot becomes
   occupied, including before its first successful NGX Create. Extend
   retirement/slot mutation and competing admission to honor the reservation.
   The ticket captures occupancy generation, instance/model handle, carrier
   handle+generation, route/layer/parent instance, history, tuning/storage,
   exact last queue identity+fence+value, pending recording and quarantine
   state. Existing `instance_id` alone starts too late to identify every slot
   occupancy. Acquire references before releasing the chain lock.
4. **Serialize the event independently of submission.** Give the existing
   process-lived completion event an exclusive wait-owner lock. Borrow it with
   a nonblocking try under the chain lock; contention declines this private
   attempt. Release `g_chain_cs`, keep the exact fence reference, perform the
   existing bounded wait, then release event ownership before reacquiring the
   chain lock. This lock must not be `LifecycleQueue::submission_lock`, and
   Execute/Signal must not acquire it. Timeout retains the pending registration
   exactly as today; no unbounded event allocation or pending-handle close.
5. **Revalidate and resolve again under the chain lock.** Check the exact
   context occupancy/model, feature liveness, configuration/history, source
   sequence/claim, native-list lifetime token/device/reset serial, queue marker
   and exact fence target. Resolve current input/view/descriptor contracts again
   and compare them with the owned snapshot. Any change declines NR; never
   rewrite history or guess identity to resume. Only a still-current context
   may receive this wait's timeout latch. Then create a fresh command-state
   snapshot and proceed to the original `RecordNrUseLocked` proof. Drop the CPU
   reservation under the lock at the successful handoff to recording ownership
   or at decline, with no gap allowing slot reuse.

The non-waited path should use the same admission contract, so the two paths
cannot diverge on color/guide/model prerequisites. Present/FG paths retain their
own ownership boundaries; they must neither borrow this reservation nor gain
new admission from it. One source must not be captured/claimed twice because
the outer dispatch was split.

## Minimum regression before enabling this cut

- Actual admission functions with two CPU threads: wait in progress permits
  unrelated submission/Signal to obtain `g_chain_cs`, while context retirement
  and slot reuse remain blocked until the preparation lease is dropped.
- Config/history/route/model/feature release, list Reset/destruction/reuse,
  queue migration, source consumption, resource/view revision and device removal
  during the wait each reject resumed private work. A stale ticket cannot set
  a timeout latch on a new context occupant or restore an old command snapshot.
- Two waiters on one queue, different exact targets, event-registration failure,
  pending timeout notification and pool/ownership contention preserve bounded
  handles/fence references and the original deadline; no submission-lock cycle.
- Source-guide capture occurs once after a valid resume; early decline records
  no new private work, and original SR/RR still runs exactly once. Retain
  generated/consumed/replay guards and the existing source receipt finish path.
- Full production queue fixture: mixed/duplicate/replayed lists, native Execute
  followed by its exact Signal, failed Signal and deferred retirement unchanged.
- Integrated canonical Core build and WARP resource/state restoration matrix,
  followed by matching game RRNR+FG captures. CPU lock-access success is not a
  game-speedup result.

No runtime source was changed in this investigation. The supported submission
QI increment is separate commit `5966d4ed7be42ea750320c7bba58456b5d60f035`;
it deliberately leaves `PrepareContextUseLocked` unchanged.
