# Volumetric clouds (work in progress)

An optional replacement for the game's painted cloud layers: raymarched volumetric cumulus that
are cel-shaded to match the toon look (or rendered realistically). Status: the cloud shader and a
preview tool work; they are **not yet connected to the game**.

## What exists

- `runtime/src/gfx/clouds.h`: the clouds as renderer-independent GLSL plus the parameter block.
  A layer of density between two altitudes over a curved sea (parabolic curvature, accurate in
  32-bit floats), shaped with value-noise FBM and Worley noise (cumulus height profile, coverage
  threshold, a low-frequency "weather" term for groups of clouds, detail erosion faded out with
  distance), lit by marching towards the sun (Beer-Lambert, powder term, two-lobe Henyey-Greenstein
  phase). After Schneider, "The Real-Time Volumetric Cloudscapes of Horizon Zero Dawn" (2015) and
  Hillaire, "Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite" (2016); no code
  from other projects.
- **Cel shading**: the light at the cloud's visible surface (where it is 40% opaque) is quantised
  into bands (`style.y`, default 3) with soft band edges (`style.z`), the silhouette is a hard
  threshold, and a silver-lining rim appears where the sun is behind the cloud (`style.w`).
  `style.x` blends realistic (0) to cel (1).
- `cloud_preview` (Vulkan builds without Metal): renders the clouds over a simple sky and sea to a
  PNG with a headless device, no game or window needed.

```sh
./build/linux/cloud_preview noon.png                              # cel-shaded, noon
./build/linux/cloud_preview real.png --cel 0                      # realistic
./build/linux/cloud_preview sunset.png --preset sunset --pitch 4  # presets: noon morning sunset storm
# also: --size WxH --coverage 0..1 --bands n --yaw/--pitch/--fov deg --time s --steps n --light-steps n
```

The Linux CI renders the presets on lavapipe and uploads them as a workflow artifact.

## Connecting it to the game (needs the game code)

1. **Hide the original clouds**: find the draw of the sky cloud packets (the Execute is
   `0256DDF8`, see `docs/decomp-notes.md`; the GameCube names are `dKyr_drawVrkumo` /
   `vrkumo_packet_class`) and skip it while the option is on.
2. **Feed the game's environment**: sun direction and colours, sky/horizon colours and the wind
   vector come from `g_env_light` (`10475A68`, GameCube layout + 0x80) and the wind state;
   weather (rain, storms) raises coverage and darkens the clouds. The camera comes from the
   `camera_draw` hook the frame interpolation already uses.
3. **Draw**: a fullscreen pass after the sky and before the translucent effects, HUD and bloom,
   at half resolution with a depth-aware upsample, limited by the scene depth (islands and
   ships in front of the clouds). Where exactly in the frame is found from a frame capture (P).
4. **Settings**: Graphics › Volumetric clouds (off by default) with cel / realistic, quality.
5. **Metal**: port the GLSL (it uses nothing Vulkan-specific besides the uniform block).

Cost is untested on a real GPU: at 960x540, 96 view steps and 6 light steps it takes about 3 s on
lavapipe (CPU). Expected work to make it cheap: half resolution, temporal reprojection (1/4 of the
pixels per frame), early exit on empty sky, baked 3D noise textures instead of procedural noise.
