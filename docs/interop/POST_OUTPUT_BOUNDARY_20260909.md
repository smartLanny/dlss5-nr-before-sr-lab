# Observe the native SR and subsequent Output boundary

The output.1 owner test still reports weak/no post effect and intermittent
flashes, including ordinary gameplay outside photo mode. Its exact-source F8
has 2040 paired stored-output samples with no invalid values. Expected/actual
mean stored RGB deltas are 0.0128343/0.0128518, with 1863/1870 changed samples.
Native Output therefore changes at the sampled NGX boundary. NR/submissions
both advance by 55 and retirement timeouts remain zero. This does not establish
the final displayed effect or the behavior of every unsampled frame.

The frozen output.1 codec was also exercised for 125 consecutive frames per
format (R10 and FP16), reusing one slot with diagnostic capture only on frames
0 and 120. All output bytes agree between sampled and ordinary frames. The
250-frame fixture passes 2502 WARP checks and 2503 NVIDIA 10DE:2C58 checks;
it uses constructed neural planes and reads each frame, so is not a game/NGX
or unperturbed scheduling reproduction.

## Passive boundary trace

`0.4.7beta-output.2` adds bounded ReShade recording-event observations:

- A thread-local scope around the native Feature1 SR call records matching
  Output barrier usage values only on the exact native command-list identity.
  Nested scopes and other lists cannot supply that call's last observed usage.
- After successful post composition, observe matching resource barriers,
  copy source/destination use and render-target binding until a same-device
  Present callback. Resource destruction and slot destruction retire the watch.
- F8 stores a coherent historical frame/config/feature window and its age.
  Copy callbacks always return false, preserving the application's commands.
  No shader, model, guide, queue, barrier or resource-state decision is changed.

These are CPU recording events. A same-device Present does not prove that the
observed resource supplied that swapchain, and recording order is not GPU
execution order across command lists. ReShade omits split barrier flags and
subresource numbers, so this is not an authoritative state tracker. Zero native
matches means unobserved, not proof that native SR left Output in UAV state.
The trace will not automatically change the existing state assumption.
Native RR calls are not wrapped by the new Feature1 call scope.

The current model input, four encode/compose shader functions and queue.3 wait
logic remain unchanged. The boundary candidate is diagnostic and does not claim
to repair the image defect. Callback conversion adds CPU overhead while loaded;
its game performance and the observer's in-game coverage remain to be measured.

## Next owner evidence

Run the same post route for several seconds and generate one F8. Compare native
call observations with the following game's first usage and copy/write events,
alongside output.1's actual stored samples. No new seven-group capture is needed.
An incompatible reported boundary warrants a stronger native-state observation
before any corrective barrier is introduced.
