# Original patrol enemy

The geometric mesh, three-bone rig, painted 128×128 PNG and five clips in
`assets/patrol_enemy.glb` are original work by Lukas S, licensed MPL-2.0.
`assets/make_patrol_enemy.py` adapts the original MPL-2.0 coral-wanderer generator
in the SDK v3 creature example. It is self-contained and includes the complete
mesh, texture and animation source. Run it with Python 3 to reproduce the GLB.
The clips are `idle`, `walk`, `flinch`, `stun` and `death`; the death pose falls
forward and holds. Units are metres, +Y up and +Z forward. No game assets,
materials, shaders, sounds or derived templates are included.

`mod.c` is original MPL-2.0 code. It uses static service buffers and 20 bytes of
snapshot-owned actor userdata. It patrols a short route, stops and faces nearby
Link, and requests its immutable declared physical attack. Runtime combat owns
health, invulnerability, grounded reaction movement, clips, defeat and rewards.
The callback polls health and stops during reactions; it never writes Link's
hearts or decrements its own health. The native heart is queued once after a
committed defeat; issuance does not promise pickup. Hero Mode may suppress hearts.

Link position is copied from the actual native Execute hook argument using the
public CC0 SDK actor layout. No native pointer is retained. NEW/RESTORED create
waits for a fresh sample, and each actor step requires a new sample serial.
Draw has no callback. The example is experimental and requires native combat,
shield, invulnerability, targeting, defeat and heart-pickup checks for the build
being distributed. Its Windfall coordinates and route require suitable ground.

Build with a PowerPC-capable Clang and LLVM LLD (set `SDK` to the guest include
directory); keep compiled outputs outside this source package while developing:

```sh
clang --target=powerpc-unknown-eabi -mcpu=750 -O2 -ffreestanding -fno-builtin -nostdlib -fno-jump-tables -ffunction-sections -fdata-sections -I"$SDK" -c mod.c -o /tmp/sdk-v3-enemy.o
ld.lld -m elf32ppc -r /tmp/sdk-v3-enemy.o -o /tmp/sdk-v3-enemy.elf
```

For packaging, place the resulting ELF at the manifest's `mod.elf` path. The
repository example deliberately contains source only. Story Notebook and a
code-mod-enabled build are required.
