# Harbor meeting

This content-only SDK v3 preview places two Uo2 residents on Windfall (`sea`, room 11). Talk to the harbor guide to request `harbor-meeting`. Install the complete folder with the mod manager, enable it with `story-notebook`, and restart. No guest-code build or external art is required.

The scene includes mandatory `steps` and an optional `native` alternative. The game plays its `turn_camera` alternative itself when the native readiness checks pass: both residents turn in parallel and the game's FIXEDFRM/UNITRANS actions control the camera. Its timing is authored independently of the fallback. If the checks fail before the native scene starts, the fallback makes each resident face a fixed point, holds the camera on them, and returns control after a short wait.

Fallback `frames` use a 60 Hz clock. Native `*_ticks` use the 30 Hz game logic clock. Coordinates are world positions; keep both residents and the camera in the same loaded room. Adding dialogue, animation, movement, branching or a following camera to the fallback makes the entire scene use that route.

See [the SDK guide](../../../docs/mod-sdk-v3.md) for the schema, native eligibility and current limits. Plus/Start skips the scene on either route, and save states made during or after it load normally (a state taken mid-scene only in the same game session). The native route has been checked with the game running at 30 fps; at other frame-rate settings it is still experimental.
