# Android fork reuse review

Reviewed `SSunnKing/ZeldaWWHDRecompAndroid---ENHANCED`, branch `enhanced`,
commit `9c502b49bcf9b11cc52b3ac64895d12de5287d44`. Both repositories include
the MPL-2.0 license. This review concerns Android hosting, not adopting the
fork's LLVM recompiler.

## Secondary display host

[DrcDisplay.java](https://github.com/SSunnKing/ZeldaWWHDRecompAndroid---ENHANCED/blob/9c502b49bcf9b11cc52b3ac64895d12de5287d44/android/app/src/main/java/org/wwhdrecomp/app/DrcDisplay.java)
uses DisplayManager, Presentation and SurfaceView, matching our host approach.
Carried over its noncancelable, nonfocusable presentation behavior so controller
keys stay with the main game activity. Also adapted its identity-checked system
dismissal handler: invalidate our surface generation and clear the host so a
later display event can recreate it. Our generation guards and pointer ownership
remain necessary for stale callbacks and touch cancellation.

The fork host has no FoldingFeature/window-posture integration. It enumerates
presentation displays; it does not divide a foldable's single logical display.
That requirement still needs implementation here. Its fixed 16:9 Java touch
mapping also cannot replace our shared scaling and display-role mapping.

## Vulkan

[present_drc_window](https://github.com/SSunnKing/ZeldaWWHDRecompAndroid---ENHANCED/blob/9c502b49bcf9b11cc52b3ac64895d12de5287d44/runtime/src/vk/vk_device.cpp)
belongs to the fork's different `runtime/src/vk` renderer. It acquires with a
10 ms timeout and submits/presents through `R.queue`; the ordinary presentation
branch locks `R.queueMutex`. This does not prove independence from secondary
WSI stalls. Retain our existing-renderer integration and measured dedicated
queue/worker path rather than transplanting that renderer. Compatibility-driver
independence remains pending here too.

## Setup service

[WorkService.java](https://github.com/SSunnKing/ZeldaWWHDRecompAndroid---ENHANCED/blob/9c502b49bcf9b11cc52b3ac64895d12de5287d44/android/app/src/main/java/org/wwhdrecomp/app/WorkService.java)
provides foreground progress notifications, but its Java job ownership is static
in-process state, it returns START_NOT_STICKY, and progress calls target the
fork's native compiler/extractor. Copying this service does not supply durable
resume or our Python pipeline. Keep our durable coordinator and service.

This is a targeted review, not an audit of every Android feature in the fork.
Before replacing remaining implementations, compare the relevant fork files
and preserve reusable behavior with focused runtime checks.

## Validation of the adapted host

`assembleDebug` passed. The APK release-content guard checked 17 files.
`tools/android/display_smoke.py --require-fence-retirement
--require-present-worker` passed on API 36 arm64 emulator `emulator-5554`
(`build/fork-host-smoke.json`): touch, role swaps, scaling, secondary removal
and recreation, held acquire/present, bounded retirement, settings restart and
orderly worker cleanup. This regression does not directly simulate system
dismissal or controller Back routing; dedicated coverage for those remains
pending. No new timing acceptance claim is made by this functional run.
