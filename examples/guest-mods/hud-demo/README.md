# HUD drawing demo

Shows a dark rectangle, an original geometric PNG and Link's hearts from the public
HD SDK. It does not modify the game. The same held draw list appears on TV and
GamePad. The top-left anchor keeps the inset fixed at wider aspect ratios.

Build from the parent folder with `make hud-demo/mod.elf`, then install this entire
folder through the Mods tab. Enable code mods if offered, confirm the package,
enable it and restart. Trust covers the ELF, manifest and package art. Android
compiles the host API but does not support guest modules.

The callback caches its PNG handle and reloads it when `wwhd_hud_epoch()` changes
on a full-state load. Element and text buffers use static mod-owned storage.
Rendering holds each list between logic steps; coordinates are not interpolated.

Art is generated from original geometry by `../generate_art.py`. Run
`python3 examples/guest-mods/generate_art.py` from the repository root to regenerate
all example PNGs, or add `--check` to verify them. Source and artwork are offered
under CC0-1.0; see `LICENSE`.
