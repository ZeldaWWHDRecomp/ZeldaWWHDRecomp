# Changelog

What changed in each update of the Wind Waker HD native port. Downloads and the same notes per
release: [Releases](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/releases). "Next update" lists
what is on `devel` and not released yet (also in the
[development build](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/releases/tag/devel-build)).

## Next update

- **Linux on Mali (panvk): no more flickering shapes in the sky.** Parts of the clouds and haze on the title
  screen and at sea flickered away in hard-edged chunks as the camera moved. Mali GPUs clip depth by dropping
  individual pixels, and the sky is drawn right at the far plane, so rounding dropped pixels that should stay. On
  the panvk driver the game now clamps depth instead (`WWHD_VK_DEPTH_CLAMP_ALL=0` turns this off). See the Vulkan
  docs.
- **World editor: island extensions can join an island's ground** (#281, thanks @KelpyCode). New ground that meets an
  island's ground at the same height (up to the 30-unit step Link and villagers walk over) counts as joined instead of
  overlapping, and an extension's sides may go into the cliff under its top; walls, raised parts and roofs still count.
  Villagers, start points and routes only count what rises into the space they need, and Windfall's kids, Tott and the
  pirates are recognised as villagers. The boat route around Windfall keeps the shop ship's real size clear.
- **World editor: placing a large model of your own live no longer crashes the game** (#281). With `add_actor`, a big
  model could be left out (its archive was being prepared again at that moment) and removing the failed actor then
  crashed the game. The model now appears as it does from the manifest, and an actor whose model can't be loaded is
  reported in the editor instead of crashing.
- **Mac: textures are as sharp as on the Wii U.** The game gives most textures a level-of-detail bias (usually one
  level sharper); Apple's samplers can't apply it, so on the Mac (Metal and Vulkan) textures were drawn one level
  blurrier. The bias is now applied in the shaders, so ground, walls and effects such as the glow under the Triforce
  chests look as intended. Windows, Linux and Android were already correct.
- **One mod that can't start no longer stops all the others.** A mod whose requirement can't be met (for example a
  GameCube game for mods that was moved or deleted) is now skipped on its own, along with any mods that need it, with
  a message in the log, in Settings → Mods and over the game saying what's missing and how to fix it. Before, it
  stopped every code mod from starting, including the Story Notebook and the sky island.
- **The glow under Triforce Shard and Chart chests no longer shows a blocky checkerboard** (#297, thanks
  @aaronlink127). The glow's texture has mipmap levels, and the second level was read from slightly the wrong address,
  so it showed scrambled blocks once the game sampled it (at high resolutions, where the game sharpens it by one
  level). Both renderers now find every level the way the Wii U does.
- **Performance report: the "uniforms" time is split into its parts** (#178): stage setup, packing, the game's
  own blocks and the support upload, so a report shows where that time goes on your device.
- **The camera is smooth again in the Boating Course minigame with frame interpolation** (#299, thanks
  @aaronlink127). The Boating Course draws two cameras at once, and after a few stage changes the two took each
  other's history slot on every frame, so the camera moved at the game's 30 steps per second while Link and the boat
  were smoothed, which made them seem to vibrate. Each camera now keeps its own history. This affected 40, 60 and
  120 fps and true 60; game logic and 30 fps are unchanged.
- **Android: setup now prepares model shaders by default, for far fewer stutters.** "Prepare model shaders
  (recommended)" is ticked on Android. After setup, the game draws every model, room and placed character once and
  visits every stage, hidden behind the setup's progress screen, so the shaders are ready before you play. On a
  Xiaomi 14 this cut shader waits on a 25-place test route from about 1,000 to about 35. It takes about 40 minutes
  on a recent phone (longer on older ones); you can untick it, and Cancel keeps what was prepared. Desktop setup is
  unchanged. Also fixed: on a fresh setup, the shaders prepared during setup's warm-up were not kept (Android and
  desktop Vulkan), and a chosen step that cannot run now says why instead of being skipped silently.
- **Mac (Metal): the distant sea at the horizon no longer flickers between runs.** Metal drew some objects straight
  from the game's memory while the game was already writing the next frame's positions into it. Vertex data the game
  rewrites is now copied when a draw is recorded, as on Vulkan. This also fixes a rare case where Metal could reuse
  overwritten settings from a recycled upload buffer. Shader settings outside display lists get the same treatment.
- **The distant sea and grass at Outset's cliff no longer change colour from run to run** (#178). The game shares
  one "current fog" between two CPU cores: while one core briefly switches it to the sea's fog, the other could copy it
  into the scene settings, so some frames picked up the sea fog. The copy now always takes the scene's normal fog,
  which is what the Wii U's timing gives. The same race in the other direction (the sea's waves picking up the normal
  fog, visible as a changing band at the horizon) is fixed too: every fog copy now reads the fog its own thread set.
  Sea and background fog are unchanged, and there is no speed cost.
- **World editor: kit models are easier to work with** (#281, thanks @KelpyCode). Replace from file and Watch file
  no longer fail with "invalid model declaration" on a model that uses the game's own materials, and they no longer
  refuse a new file that has a collision mesh. An object the game left out at start-up because something was in
  its way now appears as soon as you move it to a clear spot. You can keep a preview picture on a game material in
  Blender: the import removes it from the package by itself and says so. Placement now checks an object by its own
  shape instead of a box around it, so a large L-shaped piece no longer "hits" villagers, start points or routes
  that sit in the open space of its corner; anything its shape really reaches still blocks it. The route around
  Windfall at sea level is now named as the boat route it is.
- **Fixed: choosing Continue after a Game Over froze the game with frame interpolation on** (#295, thanks
  @aaronlink127). The Game Over screen's logic runs from a drawing step, so the extra in-between frames moved it
  forward before the save question was open, and the game picked "To Title" by itself. That logic now advances once
  per game step, as at 30 fps. Continue, Quit to Title and Save/Cancel work at 30, 40, 60, 120 and 240 fps.
- **Scenes look the same every run, like on the Wii U.** The game rewrites some per-frame scene settings while the
  renderer is still drawing the previous frame's display lists, so a few draws could pick up the next frame's values
  and some pixels (the distant sea at Outset's cliff, the Forbidden Woods canopy) changed from run to run. Display
  lists now draw with the settings as they were when the game submitted them, as the console's GPU does (Vulkan and
  Metal). Cost on an Android phone: about 2% render time in the heaviest scene tested, none measurable elsewhere.
- **Floating-point values are now loaded and stored exactly like on the Wii U.** The game's float loads and stores
  now move the bits the way the console's CPU does: special values (signalling NaNs) are no longer changed on the way,
  and a value that doesn't fit single precision is stored the way the console stores it. In our test runs the game
  plays exactly the same as before, with no measurable speed cost (measured on an Android phone). The decompiled
  game code is now checked against the same exact behaviour.
- **Fixed: building code mods could fail on Windows with "Access Denied"** (#282, thanks @Eggtooth). While setup
  builds code mods, the game reads the build's progress file for the progress bar. Windows refused to replace that
  file while the game had it open, so the build stopped at a random point. Setup now replaces it the way Windows 10
  and later allow for open files, and a progress update that still can't get through is skipped instead of failing
  the build.
- **Quit button on the file select screen** (#278, thanks @Tamryell and @CaptainLibrary). The save-slot screen has a
  **Quit** button at the bottom in the game's own style: press **X** (or click it) and confirm with Yes (No is
  selected first). Handy with a controller, on the couch or with remote play. Settings → Controls can hide it.
  The button and its Yes/No question use the game's own lettering, button plates and selection highlight, read from
  your game files, so they look like the game's Return/Erase/Copy/Start buttons.
- **Shader preparation's models step is steadier.** Four crashes in the step are fixed (a title-screen button
  pressed twice, an area requested before the previous one was released, and two cases where it released files the
  game was still using). Four characters that always stopped the step are now skipped from the start instead of after
  a restart. The step records the same shaders as before.
- **Controllers can keep playing while the game isn't focused** (#267, thanks @Linken17). Settings → Controls →
  **Controller input when the game isn't focused** (off by default): for a second screen, streaming or remote play.
  Only controllers: keyboard and mouse still work only while the game window is active, and a controller press in
  the background never opens Settings. Not on Android. On macOS it needs macOS 11.3 or later.
- **Fixed: Android said "Not available for this version of the game" for every code mod in Mods > Community**
  (ZeldaWWHDMods#6, thanks @alexander-beepx). An old rule from before Android supported code mods still blocked them in
  the Community list. They can now be installed on Android like on desktop (Code-mod support must be on).

- **Fixed: Link stayed invisible after some conversations with frame interpolation on** (#283, thanks @aaronlink127
  and @Omaley360 for the report, save state and confirmation). Some conversation shots hide Link for a moment; with
  interpolation at 60 fps and above he could stay hidden for a long while afterwards (the cabana door on Private
  Oasis, residents on Windfall). He now reappears exactly when he does at 30 fps.
- **Fixed: shader preparation failed when the game is on a network share (NAS)** (#290). Setup created its shader
  folder with "only this user" permissions that many network shares can't apply, so even setup itself couldn't write
  into it. The folder now gets the same permissions as the rest of the game folder. Also, "Setup in a console window"
  no longer warns that network paths are not supported.
- **Less shader stutter on Windows, Linux and Mac (Vulkan)**: after setup's models step, the game now builds every
  recorded shader pipeline in the background while you play, on low-priority threads, starting once the game is in
  play so the boot isn't slowed. In our tests over 19 places, pipelines built mid-play dropped from about 2,100 to 9.
  The models step now also visits every room with a player start, every sea island and each particle effect once,
  and a stage whose name matched a skip entry is no longer left out (the whole sea was skipped before).
- **World editor: use the game's materials without MCP** (#280, for mod makers). The model import has a new option
  **Use the game's materials (kit)**: pick the room (Link's by default), copy its material names for Blender, and the
  dialog shows which of your materials draw with the game's own textures, with a picker to map your own names onto
  the room's materials.
- **World editor: replace, watch and remove imported models** (#287). **Replace from file…** re-imports a model under
  the same name, and everything placed with it updates, collision included. **Watch file** re-imports it whenever
  you save it from Blender. **Remove** lists anything still placed with the model and asks first; Undo brings it all
  back. Also fixed: a removed object no longer counts as ground for the editor's own checks.
- **Fixed: characters didn't show up in a scene's Demo step** (#285, for mod makers). Link now starts at his first
  position key instead of walking there from wherever he stood, and game characters created for a demo (a pig, for
  example) follow their keys instead of their own routes. The editor warns in yellow when an actor's keys never come
  into the demo camera's view, and a first position key is placed at Link instead of far out at sea.
- **Fixed: Fast Forward's 3x and 4x ran only as fast as 2x** (#268). On a 60 Hz display the game waited for the screen
  after every step, which capped it at twice the normal speed. While Fast Forward is held, the game no longer waits
  for the screen, so 3x and 4x really run three and four times as fast, with or without frame interpolation and
  in True 60. Normal pacing comes back cleanly when you let go.
- **Experimental setup step: "Preparing shaders: models"** (off by default). When switched on in setup, it draws every
  object model, every stage's rooms and the characters and objects they place, the sky and weather at different times of
  day, plants and the file select once with the game's own code, so their shaders are ready before you play: in our test
  route, shaders compiled during play dropped from 277 to 33. It takes about 8 to 10 minutes on a Mac.
- **Smaller, faster shader caches:** each shader is now stored once in the pipeline recipes and the SPIR-V cache (the
  SPIR-V cache had stopped growing at its size limit), and shaders of an object's models are prepared as soon as the
  game loads that object.
- **Frame interpolation no longer changes the game** (40, 60, 120 and 240 fps). Extra frames drawn between the game's
  steps used up random numbers and decided what was visible with the blended camera, so enemies, effects and sounds
  could play out slightly differently than at 30 fps. The game now plays exactly as at 30 fps (at 40 fps also while
  Z-targeting); only the picture is smoother.
- **40 fps without judder** (#254): every frame now shows the game at its exact point in time (¾ of a step apart),
  instead of half, half, one and one step in turn. Camera turns are even.
- **Even more moves smoothly** with frame interpolation: camera shake, the sky's clouds, eye movements, lava, scene
  wipes, chains, the Swift Sail and boost blur, debris, Valoo, light beams and searchlights.
- **Fixed: fade-ins and fade-outs flickered** with frame interpolation (#273). Fades between scenes are smooth again
  at 40, 60 and 120 fps, including fades to white (for example leaving the Forbidden Woods). Wipes in cutscenes keep
  the same length at every frame rate.
- **More moves smoothly** with frame interpolation: boss hit flashes, sand floors, cloth and flags, water masks, shadows
  and mod HUDs. Objects at the screen edge no longer pop in or out between the game's steps.
- **Mods can use the game's own HUD art** (for mod makers): a guest mod can draw any texture or pane of the game's
  2D layouts (map frames, icons, markers) from the player's own game files, check whether one exists first, and
  ask for it enlarged smoothly and sharpened.
- **Fixed on Android: the "too warm" pause during setup could flash on and off in a loop** (#277) on some handhelds
  (AYANEO Pocket EVO and Pocket S). A heat pause now needs a real temperature to confirm it and always waits for you:
  **Continue anyway** works at any time, and **Continue** appears once the device has cooled down. Setup never goes on
  by itself after a heat pause, also when the app was in the background.
- **Fixed: some effects were drawn in the wrong place at aspect ratios other than 16:9** (#275): the elemental arrow
  glow and rupee sparkles now sit where they belong at every aspect ratio, also after switching it while playing or
  loading a save state.
- **Fixed: a possible crash at 40 and 120 fps when the scene changes** (a door, warp or loading zone while the camera
  moves), in development builds since the frame interpolation logic changes. Also: archive loads and the camera now
  follow the game's steps exactly at every frame rate, so scene changes and distant islands play the same as at 30 fps.
- **Fixed: the mouse camera's up/down was inverted in some interiors** (#274), for example Grandma's house and the
  Pictograph shop. Mouse and right stick now look up and down the same way everywhere, with and without "Invert camera
  up/down", and switching that setting while playing no longer leaves the camera inverted until a restart.
- **World editor: custom items** (#269, #270, for mod makers): opening a chest with a custom item no longer crashes,
  and the item's own icon is held up over Link's head. New items and icon changes now work while the game runs (a chest
  with a new item appears at once); changes that still need a restart are marked "Takes effect after a restart", and
  "Restart and return here" restarts the game and brings you back to the same spot.
- **World editor: own 3D models** (#280, for mod makers): own models now work in the Windows release (their converter
  could not start there before). A model built on an exported island stands exactly where it was modelled, the editor
  says why a model can't be shown instead of asking for a restart, and the import fixes common Blender exports by itself
  (several objects, quads, vertex colours, blended materials, large textures) with a note for each fix. A model with
  its own `_col` collision meshes now gets that collision when added as **Your model**, so Link walks on it and its
  walls stop him, and the import warns when a mesh's normals point inward (the model then looks dark where it is lit).
  The editor docs explain how to use the game's own materials and textures for a model. All of the game's model tools
  also start correctly under the Windows release's Python (no more "No module named …").
- **New setting: Shader compile** (Settings → Graphics). **Off** (the default) draws everything exactly and waits for
  shaders that aren't ready yet, as before. **Fast** keeps playing while new shaders compile and may leave out a few
  objects for a frame or two, for fewer stutters on first visits.
- **Fixed: the sun's glare could make the game behave slightly differently from run to run.** The game's check whether
  the sun is hidden got its answer whenever the GPU finished; it now arrives at a fixed point, so the game behaves the
  same regardless of timing (the glare reacts one game step later than on the Wii U; no cost).
- **The Goron merchants for mods** (for mod makers): the three Goron traders can be placed on a mod island and talk
  with your own text. Also new in the catalogue: torches, flames, fire jets and lava bubbles, Dragon Roost's bomb flower,
  the dungeons' stone block, rocks, boulders and blocks that break (and stay broken until the room loads again), more of
  Dragon Roost's and Fire Mountain's rocks, and Zunari's stall in three sizes.
- **Mod islands are seen from farther away** (for mod makers): island grounds, kit models and distant views stay visible
  across their square and into the next ones, with the game's haze. A stage entry can be a hole Link falls into, and a
  scene's area trigger can play again once Link has left the area.
- **Fixed on Android: false "too hot" warnings on some handhelds** (#254). Some devices report Android's highest heat
  level while they are cool (e.g. the AYANEO Pocket EVO at a 30 °C battery). Setup, the heat guard and the performance
  report now believe that only together with a real high temperature (battery 40 °C or more, or processor 90 °C or more).
- **Smoother frame interpolation** (40, 60, 120 and 240 fps). More things now move smoothly between the game's
  steps: the letterbox bars in cutscenes, the sword trail, the hookshot chain, the boomerang trail, the cannon's aiming
  arc, hair, strings and ropes, the sun, moon, lens flare and sea spray, the ship's hull, grass, trees and flowers when
  hit, and fallen trees as they fade out. Stage fades no longer run faster at higher frame rates, and the camera no
  longer jumps when it moves closer to Link, except when a wall pushes it in (#251).
- **The performance report shows the screen's refresh rate**: the rate the screen actually runs at, the rate the game
  asked for on Android, and whether the chosen frame rate is shown evenly on it (e.g. 40 fps needs a 40, 80 or 120 Hz
  screen) (#254).
- **Fixed: the game only started through the launcher** (#266). Started directly (e.g. `bin/wwhd` or `wwhd.exe`) from
  another folder, it couldn't find the game files. It now always works in its data folder; `--data-dir <folder>`
  chooses one explicitly.
- **A clear message when the mod compiler is missing** (#266). Code mods such as the Story Notebook need it; the game
  now says so over the game and in the Mods tab, with how to restore it (setup → Repair). Content mods (signs, stages,
  models) no longer need it.
- **Fixed: `"Wind Waker HD.exe" --editor` started the game without the world editor** (#266). The launcher now passes
  game options such as `--editor` on to the game on Windows, macOS (`open "Wind Waker HD.app" --args --editor`) and
  Linux (`wind-waker-hd --editor`).
- **Calm water around mod islands** (for mod makers). An island on the open sea can set a calm-water zone, so the
  waves fade out around it the way they do near the game's own islands, and the boat can land at a low pier without
  Link being washed off. Set it in the manifest or in the world editor's Inspector. See [world objects](docs/world-objects.md).
- **Lava for mods** (for mod makers). A `col_lava` ground burns Link exactly like the game's lava (he jumps, loses half
  a heart and is sent back) and makes bombs explode, in mod stages, world objects, island grounds and models with
  several materials. Mod stages and the world editor can place the game's moving lava floor and Dragon Roost's heat
  shimmer; on the sea the water tool can make a lava pool (e.g. in Fire Mountain's crater) that glows and flows like
  Fire Mountain's own lava. See [mod stages](docs/mod-stages.md).
- **Fixed: water in mod stages was not swimmable.** Its surface sat one level too deep for the game's water check, so
  Link stood on it; it now works like the game's own water.
- **Game materials on your own sea models** (for mod makers). A model placed on the sea can name the game's own
  materials (rock, ash, planks, …) instead of bringing textures; they are built from your game files and lit like
  the islands' own buildings at every time of day. Materials with your own small textures can sit next to them in
  the same model, and such a model can also be a distant view. Collision bodies can now have up to 8192 triangles,
  and the parts of one composition are placed together. See [world objects](docs/world-objects.md).
- **Bigger islands for mods** (for mod makers). One collision body on the sea can now be as large as a whole sea
  square (1 km instead of 200 m), like the game's own islands, so a big island no longer has to be split into
  sections with seams. The editor's focus frames objects that size.
- **Detailed performance profiling is now a setting, off by default** (#261, thanks @pancreations). The detailed
  per-draw profiler and the periodic performance lines in the log no longer run unless you want them: Settings →
  Graphics → Overlay → Detailed performance profiling turns them on at once, without a restart. Then play a few
  seconds and use "Copy performance report" for the detailed sections; with it off, the report says so.
- **Fixed: Link's fist could float off his arm with frame interpolation** after the Triforce cutscene, e.g. in
  Hyrule Castle (#255). A part of a model that did not move no longer makes the whole model skip smoothing.
- **Fixed: the game could hang after previewing a scene in the world editor.** A scene that creates an actor
  made the game draw it twice, and its models looped in the draw list. Scene actors are now created like the game's
  own, and a model entered twice is drawn once instead of looping.
- **Fixed: the world editor crashed when jumping into a mod's own stage** (e.g. to a quest step there). Every editor
  jump now uses the stage's game name and checks the room, and the game refuses a jump to a place it can't load.
- **Meta Quest: a floating theater** (#259, thanks @pancreations). On a Quest the game shows on a flat or
  curved screen in your room, with controls for 2D/3D, size, distance, passthrough and recentering; the controllers
  work the toolbar and the name keyboard, a gamepad plays the game. If the VR display stops during play, a "VR display
  stopped" panel says so instead of a black view. Other Android devices are unchanged.
## v0.4.0

- **Fixed: switches and doors waited for a restart in the world editor.** With all of the editor's live kinds
  installed, the table for the port's own actor types was full, so switches and doors fell back to "after a restart".
  They now appear at once like the other actors, and a failure names its cause in the log.
- **Mod stages: the light of a place of the game, the game's trees, joined rooms** (for mod makers). A mod stage
  can take its light from another place, e.g. `"light": {"stage": "sea", "room": 44}` for Outset's daylight that
  follows the game's clock; it can place the game's own trees (small, bushy, large with a scale, palm) as room
  actors; and rooms joined by an open passage are now drawn together. The editor accepts parameters of two types
  (such as a kit given by number or by stage and room). See [mod stages](docs/mod-stages.md).
- **Dynamic resolution** (all platforms, off by default; idea from the GreenNaugahyde Android fork). Settings →
  Graphics → Dynamic resolution lowers the internal resolution in small steps (down to 75% or 50%) while the
  graphics chip holds the game back, and raises it again when there is room. The picture keeps its size and is scaled
  smoothly, and a change of level causes no stutter. It helps on devices where the graphics chip, not the processor,
  is the limit; it costs some extra graphics memory (about 35 MB on a phone). See the Android and Vulkan docs.
- **Fewer shader variants on Metal (macOS)** (idea from the GreenNaugahyde Android fork). Metal now keys shaders on
  the same narrow state as the Vulkan renderer, so the same scenes need far fewer shaders and pipelines (in our Outset
  test about 500 instead of 3,500 shaders); existing shader caches keep working. Pictures are unchanged.
- **Crash logs and performance reports list the renderer's live shaders, pipelines and memory mappings**, which helps
  us find out-of-memory crashes.
- **Less GPU work on Vulkan** (idea and first versions from the GreenNaugahyde Android fork). Shaders use the slower
  exact multiply only where a value may be infinite, and rendered mip chains are built only as deep as the game samples
  them, starting from the first changed level. On our test phone the GPU time per frame fell by about 8–9%; pictures
  match the previous version (a few pixels differ in the lowest bits).
- **Export the game's islands for Blender** (for mod makers). The editor's new `export_glb` command (and
  `tools/story_mods/export_glb.py`) writes a game room, e.g. Outset or Windfall, or one building as a GLB file with
  textures, the game's material names and optionally collision by ground type and actor markers. It is a reference
  for scale, style and material names when building your own islands; the files are your own copy of the game's
  content and must not be shared or put into mods. See [world objects](docs/world-objects.md).
- **Fixed: mod stages in release builds.** The stage builder was missing from release packages, so mod stages,
  `list_kit_materials` and the stage templates failed there; development builds were not affected.
- **Mod stages and scenes: quests through stages, cutscene clips, carrying Link** (for mod makers). Chests,
  defeated Darknuts and warps in a mod stage can move a quest on (`then`), quest places can point into the mod's own
  stage, scenes can use clips from the game's cutscene archives that play once and hold, Link's face animations and
  blending, a smooth `blend` when attaching one actor to another (e.g. the Helmaroc King grabbing and carrying Link with
  the game's own capture animation), and a scene spawn that drops Link in from above.

- **Fixed: Continue last session after quitting inside a dungeon, cave or house.** Starting the game with
  **At start → Continue last session** (or a save state chosen to load at start) put Link at the save file's start
  point on Outset and then left the screen black. Link now arrives where you left off; loading states during play is
  unchanged.
- **Mod SDK: mod dungeon maps for HUD mods** (for mod makers). A guest mod can read the current room's map of a mod
  dungeon (rooms, visited rooms, Link's position, and with the compass the chests), so minimap mods can show mod
  dungeons too.
- **Vulkan stereo (3D) improvements** (#257, thanks @pancreations). Formats that can't be scaled fall back to
  full-size eyes instead of drawing into a cleared image, and stereo modes reuse their image memory between frames.
  Nothing changes with stereo off.

- **Less render-thread work on graphics register writes** (all platforms; idea from the GreenNaugahyde Android fork).
  Register writes look up what they affect in a table instead of walking ranges, shader constants take a fast path,
  and the hot paths no longer use per-thread variables: about 0.3–0.5 ms less render-thread time per frame in our tests.
- **Shadows on GPUs without depth clamp** (older Mali and PowerVR phones; idea from the GreenNaugahyde Android fork).
  Such GPUs used to fail on the game's shadow draws; the port now clamps depth in the shader instead.

- **Fixed: the Pirate's Charm shook without ringing at 60 fps and above** (#262). With frame interpolation the
  game's on-screen interface logic also ran on the in-between frames, where sounds are skipped so nothing plays twice,
  and the charm's ring always landed there. It now runs once per game step at every frame rate, as at 30 fps; this
  also brings the interface's other small animations back to their original speed.

- **Scenes and kit materials for mod makers.** A demo actor can ask for its own copy with `"spawn": "always"` (for
  example the Helmaroc King, while the room's own one waits hidden for its story moment). A room without a material
  mapping now uses the kit's main material instead of an arbitrary one, which fixes streaked or black ground. Scenes
  without text, sounds or story actions no longer need the Story Notebook. New editor and MCP commands:
  `list_kit_materials`, `edit_demo_actor`, `set_package_info`, and `focus` with an eye point.

- **Mod dungeons: sky islands and boss rooms** (for mod makers). A stage can borrow the sky of a game stage (day
  sky, stars or dusk) over your own models; `list_stage_templates` in the editor and MCP lists which stages have a
  sky, a boss door and music to borrow, and the Stages tab can filter for skies. New entry style: an updraft that
  lifts Link into the stage. A big chest can hold a heart container, and a warp can wait until a chest is opened.
  Talks and scene ends can take Link into a stage. A stage can bring its own shader template so it doesn't stutter
  on the first visit (installed after setup: run setup's Repair). Game Over in a mod stage restarts at the stage's
  start.
- **World editor: scenes play right away, and easier demo steps** (for mod makers). A new scene can be played with
  Play scene or Play step without saving and restarting first. In the Scenes window the step row no longer hides its
  buttons on narrow panels; the buttons say "Duplicate scene" / "Remove scene…" and "Duplicate step" / "Remove step",
  a step can be removed with right-click or Delete, and Undo brings back a removed scene. A demo step's actors and events
  are now picked from lists instead of typed as JSON: game characters, models and your mod's characters; sounds (with a
  Play button) and particles by name; joints, animations, Link's modes, time, rain and messages. The JSON stays under
  "Advanced". Full save states made by an older build in editor mode can't be loaded by this one.
- **Water tool: new ponds are swimmable, and pond surfaces always appear.** A new pond's level now defaults to its
  depth above the shore, so Link can swim in it right away; banks stay compact, and nothing reaches down to the sea
  floor. Some pond outlines left the visible water out because its surface model couldn't be prepared; that is fixed,
  and the editor names the reason if a model ever can't be prepared. Such a model no longer stops the game at start.
- **Less render-thread work on the Vulkan renderer** (#178). Shader programs are fingerprinted again only after the
  game writes to them, and texture invalidation now looks only at the textures it can reach. On a Snapdragon phone this
  cut the render thread's CPU time by about 11% in Windfall; the picture is unchanged.

- **Eighteen more islands to place** (for mod makers). Shark, Horseshoe, Rock Spire, Thorned Fairy, Seven-Star,
  Overlook, Stone Watcher, Cliff Plateau, Angular, Boating Course, Gale, Headstone, Greatfish, Cyclops Reef,
  Two-Eye Reef, Diamond Steppe, Mother & Child and Fire Mountain join the island grounds, built from your own game
  files. Diamond Steppe and Mother & Child keep their own moving water. Pits, holes and doorways send Link back to
  the restart point. Fire Mountain comes without its lava for now, and Bird's Peak is too large to offer.

- **World editor: building mod levels, also with an AI assistant** (for mod makers, preview). A new Stages tab creates
  a level from your room models (import, add, build, warp in), adds rooms, places entries, exits, doors with locks,
  chests, enemies, pots and return warps where Link stands, and moves them with handles in the picture. Check lists
  unreachable rooms, keys behind their own doors and room models without UVs or named materials. Every step is also an
  MCP command, so an assistant such as Claude Code can model rooms in Blender and build and look at the level in the
  game; see the new guide "Building a dungeon with an AI assistant".

- **Fixed: ponds on a mod's own island** (water tool). A pond drawn on a terrace of the mod's own island model no
  longer turns into a column down to the sea floor and is no longer refused as "in the way": the water's level follows
  the ground at its shore, the island's own surfaces count as ground, and only what really rises through the water (a
  wall, a pillar, a house) blocks it. Messages now name the game place that blocks a spot and where it is.

- **Mod SDK: a dungeon map for mod dungeons** (for mod makers, preview). A mod dungeon with a map shows it on the
  GamePad's map page (and on the TV's map tab with the Pro Controller): rooms seen from above, made from the rooms'
  floors, the rooms Link has visited, where he stands, and with the dungeon map every room; with the compass also the
  chests (open or not) and the boss door. Only Link's floor is shown for now.

- **40 fps mode.** A new frame rate next to 30, 60, 120 and 240 fps: three game steps over four frames, at full game
  speed. It suits screens that run at 40, 80 or 120 Hz, such as the Steam Deck LCD at 40 Hz or the OLED at 80 Hz, where
  every frame is shown for the same time. On Android the app asks for a matching screen rate; some phones choose the
  rate themselves, and on a 60 Hz screen 40 fps runs unevenly (Settings says so). Keep game speed works as at 60 fps.

- **Mod SDK: dungeons in mod levels** (for mod makers, preview). A mod level can now be a small dungeon: rooms taken
  from any game room or the mod's own models, small-key, boss and shutter doors, chests with a small key, map, compass
  or boss key, enemies (ChuChus, Bokoblins, Keese, Darknuts), pots and a return warp. Each mod dungeon keeps its own
  keys, opened chests and switches; the game's save still only stores a normal entrance. See the example
  "mod-stage-dungeon".

- **Mod SDK: new levels of their own** (for mod makers, preview). A mod can add a new cave or dungeon level with its
  own rooms, reached from a box in a game level and left through a floor exit. The level is built from the player's
  own game files when the game starts, with the look and music of a game level the mod names; doors between its rooms
  work. Saving inside it is safe: the game's save always keeps a normal entrance, so the save still loads with the mod
  removed, and with the mod the Quest Log continues inside. Save states made in a mod level ask for its mod. The editor
  lists mod levels for warping. See the example "mod-stage-cave".

- **Automatic picture-in-picture shows the Wind Waker's song list** (#256). With the GamePad screen set to Automatic
  picture-in-picture, the GamePad picture now also appears while Link conducts with the Wind Waker, with the song list
  and the conducting guide, and goes away again when he stops.

- **Fixed: Link's fist no longer flies in from elsewhere with frame interpolation** (#255). When Link's hand pose
  changed (for example from standing to running, or after targeting), a hand pose could appear for one in-between
  frame at the spot where it was last used and fly to his hand, most visibly at 120 fps and above. A part of a model
  that comes back after being hidden is now drawn where it is, without blending; everything else blends as before.

- **Mod SDK v3 and the editor: Kargarocs with riders and Wizzrobe summoners** (for mod makers). A Kargaroc can carry a
  Moblin or Bokoblin along a mod path and drop it at the path's release point. A Wizzrobe summoner teleports between the
  points of one path and calls waves of enemies at the points of the next, in the game's own wave sizes (up to 20);
  summoned enemies give no drops. The editor sets up both paths when a summoner is placed.

- **World editor: a water tool for ponds, rivers and waterfalls** (for mod makers, preview). Under Add > Water, click
  points on an island's ground: a pond gets a level handle, a river a band with flow arrows, shallow parts to wade and
  a reverse button, and a waterfall forms at a river's step or from two points. Link swims, wades and falls as in the
  game. Banks slope into the island's own grass and sand, the water is the game's own, and foam shows only at the
  shores. Check marks problems in red in the picture. The same is available as MCP commands. On the sea only; a pond
  can't be dug into the ground, so on a slope it sits in a grassy mound.

- **Fixed: the world editor no longer crashes at start when code mods are off.** With Code-mod support turned off
  (and in the decomp version), `--editor` now starts; live editing is off and the editor says so, and edits are still
  saved.

- **Mod SDK v3 and the editor: paths, the wall cannon, warship routes and wandering Floormasters** (for mod makers).
  A mod can draw lines and areas in a room (up to 16 per mod) and its enemies can use them. The game's wall cannon can
  be placed with an area as its firing range: it turns to Link and fires while he is inside, and a bomb destroys it,
  with the mod's own defeat flag and drops. An enemy warship can sail a route back and forth, firing at Link, and a
  Floormaster can glide along a line. The editor draws and edits paths in the picture, and its Check warns when a
  ship's route runs over land or a Floormaster's line leaves the floor; the same is available as MCP commands.

- **Mod SDK v3 and the editor: every placeable villager can walk a route** (for mod makers). The Windfall villagers
  without a walk of their own (the old women, the young women and the large sailors) now walk their routes with
  another villager's walk, fitted to their own bodies when the game starts, from the player's own files. Feet stay on
  the ground at any walking speed. Full save states made with the editor before this update can't be loaded.

- **Keep game speed at 60 fps holds full speed on devices just under 60 fps** (#254). With Keep game speed on, a
  device that draws a little under 60 fps (for example a phone at 57-58 fps) no longer runs the game slightly slow, and
  a short hitch no longer costs game time: the game skips exactly as many in-between frames as it needs, without
  waiting after a skip. A slow device that takes 20-25 ms per frame now shows a third to two thirds of its in-between
  frames instead of almost none, at full game speed. 30 fps, 120/240 fps and Keep game speed off are unchanged.

- **Mod SDK v3 and the editor: sea and dungeon enemies** (for mod makers). The Kargaroc (flying or perched, without
  riders), the red Wizzrobe with its fireballs, the enemy warship (at sea only; its cannonballs hit only Link and his
  boat), groups of one to five Gyorgs (they appear once Link has the boomerang), the Floormaster (it grabs Link and lets
  go again, it never takes him away) and the Big Octo as a target whose eyes are shot out (no swallowing, no whirlpool)
  can be placed, each with the mod's own defeat flag, drops and defeat switch. Defeating them by a mod doesn't count
  toward the game's sea kills.

- **Fixed: things can now be placed on top of a mod's island** (houses, signs, villagers, enemies and other world
  objects); the island no longer counts as "in the way". Objects only block each other where their shapes really
  overlap, so turned buildings next to each other are no longer refused. In the editor, clicking the open sea now
  places an island (or anything else) on the water surface.

- **Mod SDK v3 and the editor: more native enemies** (for mod makers). Stalfos, Redead, Mothula (on the ground or
  winged), Morth, Magtail, Poe, Armos and Armos Knight can be placed, each with the mod's own defeat flag, drops and
  defeat switch, live in the editor. What holds Link (a Redead's scream and bite, a Poe's possession, clinging Morths)
  lets go when the enemy is removed, its room changes or a state is loaded. A placed Stalfos does not split in two under
  a spin attack, and a placed Mothula does not release Morths; place Morths next to it instead.

- **Animated mod models can have several materials too** (for mod makers): up to 16 per model, each with its own
  texture, opaque or cut-out (for leaves, fences, hair) and optionally double-sided, with the same texture budget as
  static models. They work in scenes and demo steps, with save states and in the editor. An example walker is included.

- **Mod SDK v3 and the editor: demo steps, cutscenes played by the game's own cutscene system** (for mod makers, preview).
  A scene can now play a demo: keyframed camera, actors flying or walking along curves (game characters, villagers the
  mod placed, the mod's own actors, and Link in his cutscene poses), messages that wait for A, sounds and effects, one
  actor carried on another's joint (for example the Helmaroc King carrying someone), and time of day and rain that come
  back afterwards. Characters a demo needs but the room lacks are created for it. Demos can be skipped, follow each other
  directly, and a choice between two demo steps picks which one plays. A time change in a demo never starts a new day.
  In the editor, a demo step's lanes appear on the Scenes timeline with keys, clips, messages and carry markers, a
  preview at the cursor and curve handles in the picture; the same is available as MCP commands. Nothing changes for
  players or mods that don't use demo steps.

- **Mod SDK v3 and the editor: more native enemies** (for mod makers). Boko Babas, red and blue Bubbles, Fire Keese,
  Octoroks and Seahats can be placed, each with the mod's own defeat flag, drops and defeat switch. A Boko Baba bites but
  never grabs Link and leaves no flying head behind; a Seahat or Octorok defeated by a mod does not count toward the
  game's sea kills. (The game has no Ice Keese.)
- **Fixed: Link's boomerang, arrows and bombs now hit mod enemies.** Thrown, fired and exploding ones were ignored
  before, also by the Bokoblins, Moblins, Peahats and ChuChus placed by mods.

- **Mod SDK v3 and the editor: island grounds** (for mod makers). Six islands without their buildings can be placed as
  ground for a mod's own island: Crescent Moon, Triangle, Five-Star, Star, Bomb and a fairy island, in the editor's new
  **Islands** list. Each is made from your own game files the first time a mod uses it, has its own collision, shows on
  the horizon from the neighbouring squares, and the ship stops at its shore. Holes drop Link back to the room's start.
- **Mod collision: water to swim in, shallow water, waterfalls, ice and void surfaces**, and a water material for ponds
  (`"material": "water"`). A pond placed above the room's height range is not found as water; the docs explain this.
- **Fixed: mod collision marked as metal hurt Link** (it was read as a damage surface, e.g. on the buoy example). Full
  save states made before this update keep their objects; their old collision is used until the room is next loaded.

- **Mod SDK v3 and the editor: island parts** (for mod makers). Smaller pieces of other islands can be placed like the
  island buildings: Dragon Roost's shrine, torii, walkways, rope posts and sea rocks, Windfall's windmill cap, and two
  Forsaken Fortress shipwrecks. They are made from your own game files the first time a mod uses them, have their own
  collision and take the light of the room they stand in. Island pieces now check the textures they are built from, so
  existing piece caches are rebuilt once.
- **Fixed: the mod actor heap diagnostic** (`WWHD_MOD_ACTOR_HEAPS`) no longer crashes when a game heap isn't set up.
- **Mod SDK v3 and the editor: rat holes and patrolling Moblins** (for mod makers). A rat hole lets out three rats (or
  bomb rats, or a mix) one after another while Link is near; each defeated rat gives the hole's drops, and the hole
  closes once all three are beaten (its `defeat_switch` is set then). A bomb rat's bomb only hurts Link and the mod's own
  enemies. Placed Moblins can patrol a route like Bokoblins.
- **Fixed: a mod enemy, prop, switch or door that the game refuses to create no longer stops the game** later.

- **Island buildings and world objects: only real conflicts refuse a place now** (doors, start points, villagers and
  their walks, chests, event triggers); pots, signs, rupees, message and hint areas and the sea's surface no longer
  count. In the editor a building can be placed anywhere: on a red spot it shows as an outline with what is in the way
  drawn in, and it appears in the game once dragged to a green spot.

- **Mod SDK v3 and the editor: the Darknut** (for mod makers), plain, with a shield, with a helmet, or the Mighty Darknut
  with its cape. Its sword and cape stay its own and go with it, it writes no game switch, and its defeat can open a
  mod door (`defeat_switch`).

- **Mod models can have up to 16 materials** (for mod makers), each with its own texture, so mod islands can mix sand,
  grass, rock and wood like the game's own islands; cut-out materials (glTF alpha clip) for leaves, fences and foliage
  cards, and double-sided faces lit correctly on both sides. New example `world-objects-w5`.
- **Fixed: island buildings in released builds.** The release packages lacked the converter the island buildings
  need.
- **Quick-use locked items** (Mods > Built-in, off by default; #196, thanks @mhbxyz): while locked onto an enemy, one
  item-button press equips and uses the bow (including fire, ice and light arrows), boomerang, hookshot or grappling
  rope; bombs are readied and thrown, and equipping the hammer completes one strike. A **Quick-use animation speed**
  of 1x, 1.25x or 1.5x speeds up the supported equip animations.
- **Camera-directed items** (Mods > Built-in, off by default; #203, thanks @mhbxyz): Link turns once to face where
  the camera looks when you use the bow, boomerang, hookshot, grappling hook, bombs, hammer or an empty bottle, and
  Leaf gusts blow in the camera's direction. Aiming and lock-on work as before.

- **Keep game speed now holds the game's speed at 60 fps when the graphics can't keep up** (#178, e.g. Steam Deck):
  in-between frames that don't fit are skipped instead of slowing the game to 21–25 logic steps a second. The
  performance overlay's slow-motion warning now appears below 29 logic steps a second (was 26) and no longer flickers.
- **Mod SDK v3: more enemies and enemy switches** (for mod makers). The Peahat and a black ChuChu can be placed;
  enemies can open doors through a mod switch once all of them are defeated (`defeat_switch`, also in the editor's
  Inspector); Bokoblins can patrol a route of the mod's own.
- **World objects: island protection now works on the sea** (for mod makers). Placement used to read sea rooms at the
  wrong coordinates and protected nothing there, and dungeon rooms with a turned map were checked turned. World objects
  now keep clear of what the island needs (villagers and their routes, doors, chests, items, switches, cutscene areas
  and the places where Link arrives) by their real size instead of wide 100 m zones, and may stand over grass and trees.
  A world object that can't stand where it is placed is left out with a log line instead of stopping the game; the
  editor shows its footprint in red with the reasons.
- **Mod SDK v3 and the world editor: island buildings** (for mod makers). Mods can place some of the islands' own
  buildings as `"actor": "piece:<id>"`: four Outset houses (their windows light up at night), Outset's lookout tower
  and well shelter, the Flight Control Platform, the Private Oasis cabana and Tingle's tower. They are made once from
  your own game files, lit by the island's light at every time of day and in all weather, and Link can walk on them;
  their doors lead nowhere. A building must keep clear of the island's villagers and their routes, chests, doors,
  items and cutscene areas (the editor shows what is in the way), and a room's buildings share 24 MiB. In the editor
  they are in the Game objects palette.
- **Performance report: it says whether Keep game speed is on** (and at which interpolation rate), and the log records
  its value and where it came from at start and on every change.
- **Controller rumble no longer stops by mistake:** the safety stop for a stalled game could switch off a rumble that had
  just been started. The game's list of villager names (for mod makers) is now kept in the mod manager's folder; it is
  built once more after the update.
- **Mod SDK v3: placed villagers can walk a route.** Uo1, Uo2, Uo3, Um1 and Um2 walk up to 15 points with their own
  walk: once, in a loop or back and forth, at their own pace or yours, with optional strolling pauses. They still stop
  to look at Link and talk as usual, and a walker that runs into a wall or walks off an edge is caught. Your own
  models' routes can also go back and forth, pause now and then, and name their walk and idle clips.
- **World editor: a Route section in the Inspector** for walking villagers and your skinned models. Add points at Link
  or the camera, drag them in the picture, reorder or remove them, and set speed, mode and pauses; the route shows in
  green, and stretches without walkable ground in red. New route commands for MCP clients.
- **Mods and the world editor can place the game's own objects by name** (`game:palm`, trees, tables, shelves, lamps,
  plant pots, flowers, nests, the Rito and sea flags, the sea lookout tower, grass, flowers and small trees): they load
  from your own game files, never from the mod. In the editor: Add > Game objects, with Variant, Turn and Scale in the
  Inspector; `list_game_objects` over MCP.
- **Mod SDK v3: thirteen more Windfall villagers can be placed** (residents uo1, uo3, ub1–ub4, uw1, uw2, um2,
  sa1–sa4); um3, sa5, ug1 and ug2 are refused with the reason.
- **Mod SDK v3: more enemies** — red, blue and yellow ChuChus (`parameters.variant`), the Miniblin, and Bokoblins with
  a shield or a machete, each with the mod's defeat flag and drops. The editor picks a villager or an enemy variant
  from a list in Add and in the Inspector.
- **World editor: the Inspector no longer says an enemy's changes wait for a restart** when they apply at once.
- **World editor: story changes while the game runs.** Quests, quest talks (also those for the game's villagers),
  letters and charts change in the running game at the next calm moment, with every edit, Undo and Redo; an item's
  name, description, amount and use change at once. A new item, its icon, held model and hold_up, and removed charts
  still wait for a restart, and the status bar says which. The Quest Log is never touched: saves and save states
  always play with the current story, and the status bar says when a renumbered step moved a loaded quest.
- **World editor: New mod…** starts a new, empty mod of your own from inside the editor (also `new_mod` over MCP):
  its own package, turned on in your mod profile and opened at once.
- **World editor: quest talks for the game's villagers.** The game's own villagers show their names in the picture;
  click one to give it a quest talk in your mod, as a new quest, a new step or a step's talk (also `list_villagers`,
  `pick_villager` and `add_villager_talk`). The villager's own dialogue and save flags are never changed.
- **World editor: placing and dragging fixed.** Clicks find the ground under the mouse again (piers, roofs and grass
  were missed or hit the sea floor); residents, enemies and props appear at once also in a mod that had no actors at
  start-up; a dragged enemy or resident now moves with the drag instead of staying at its old spot; and a resident's
  type is chosen in Add and in the Inspector.
- **Story mods: Bokoblins see Link.** Bokoblins placed by a mod stood blind and never attacked; they now notice Link
  at the game's usual range and fight. (A save state with such a Bokoblin from before puts it aside.)
- **World editor: the docked panels react to the mouse again.** Since the dockable panels arrived, the full-screen
  picture could sit in front of the docked panels, so they got no hover or clicks on any renderer; the picture now
  always stays behind them.
- **Android: handhelds with a fan keep 60 fps longer** (#247). Their heat guard now pauses 60 fps at a thermal headroom of
  1.00 instead of 0.90 (Relaxed: 1.10); the AYANEO EVO and KONKR Pocket FIT Elite ran 60 fps well around 0.90-0.96.
  Phones keep their limit.
- **World editor: native scene previews after loading a save state.** A scene's native version now also plays in the
  preview after a save state was loaded or after a preview fell back to the scene's steps, and the editor says why
  when it can't restore the world after a preview.

- **Without mods and without `--editor`, the world editor and the Mod SDK v3 stay completely off.** Nothing of
  theirs runs or reserves game memory then, so full save states made without mods by builds from before 17 October
  load again.

- **Story mods: more of a quest without code** (for mod makers). Content packages may hold only letters, charts,
  items or quests; letters can wait for a quest or flag and a number of game days (`when`, `after_days`); characters and
  scenes can give a treasure chart; items with `hold_up` are held over Link's head when he gets them; placed chests can
  hold more rupees, a heart piece or container, or the mod's own items (with a "Chest holds" picker in the editor); and
  `reset_quest` also clears the quest's charts and items.
- **World editor: letters, treasure charts and items** (`--editor` and its MCP tools, for mod makers). A new Story tab
  and commands create, change and remove letters on their own, treasure charts (with the X placed at Link or the
  camera) and items (with icon import). Removing something still in use is refused with the list of its uses.
- **Android: heat guard for handhelds with a fan** (#247). AYANEO, KONKR, AYN and Retroid handhelds now keep 60 fps
  until they are much hotter than phones, and the new **Settings → Graphics → Heat guard** (Normal / Relaxed / Off)
  sets how early 60 fps pauses when the device gets hot. Phones keep their current limit by default.
- **Android: even frame pacing at 60 fps.** A frame that is ready early now waits a few milliseconds, so each frame
  stays on screen equally long. With **Record graphics on a second thread** on, a third of the frames on a 120 Hz phone
  screen used to stay for one or three refreshes instead of two (a slight judder); now almost all stay for two. The
  game's speed is unchanged. On by default on Android.
- **World editor: scene editor, live actors and smooth dragging** (`--editor`, for mod makers). A new Scenes window
  edits a scene's steps, takes camera keys from the fly camera, shows a timeline to scrub and retime, and sets choices,
  walking paths, triggers and the native version; scenes preview in the game with Skip and Stop. Pots, barrels, crates,
  switches, doors and enemies now appear and change live in the room, and dragging an actor moves it smoothly in place
  (one undo step). The editor and its MCP tools also cover the new quest forms: talks, flag and multi-quest conditions,
  finishing a quest and places inside buildings.
- **Mod SDK v3 preview: world objects** (for mod makers). Mods can place solid scenery and walkable islands in the sea,
  with collision from the model and wood, sand or grass ground; objects that bob on the waves or drift and can be
  ridden; surfaces Link can climb, hookshot and grapple; islands that are visible from neighbouring sea squares, get
  a ring on the sea chart and an arrival notice; and animated coast foam that uses the game's own foam look.

- **GamePad picture-in-picture: simpler settings** (#221). Settings → Display → Picture-in-picture now has one slider each
  for size (5–100 %) and opacity (10–100 %). Dragging the picture places it freely, and clicking one of the nine circles
  in the diagram snaps it to that spot.
- **Mod quests: finishing, flags and places inside buildings** (for mod makers). A character's dialogue or a scene's end
  can now finish a quest (the reward is given once), characters and quest steps can react to the mod's own flags or to
  several quests at once, a character can offer step-specific conversations, and a quest step can point into a
  building, cave or dungeon (the notebook shows where, and the sea chart marks the island).
- **Graphics: toon clouds, horizon haze and cloud shadows** (Settings → Graphics → Effects, all optional).
  **Cloud style** adds *Toon clouds (Lite / High)*: puffy, cel-shaded clouds that follow the game's own clouds, weather
  and time of day (Original stays the default; Lite is the lighter one for weaker devices). **Horizon haze** (100 % =
  original) gives a clearer, deeper blue sky when lowered together with Bloom strength. **Cloud shadows** (off by
  default) let the drifting clouds cast soft shadows on the sea, islands and characters. Stereo 3D keeps the original
  clouds for now.
- **Mod SDK v3 preview: native scenes** (for mod makers). A scene can include an optional native `turn_camera`
  version that the game's own event system plays: two residents turning toward each other and a camera move. It can be
  skipped with Plus and survives save states, and the scene's normal steps play whenever the native version can't run.
- **Android/Vulkan: texture decoder self-test.** Phones that decode compressed (BC) textures in a compute shader now
  check that decoder at start-up and switch to the CPU decoder if the graphics driver gets it wrong (Qualcomm's driver
  762 miscompiled it, leaving speckled edges on the HUD hearts and streaks on water when that path was used). One rare
  case in signed BC4/BC5 textures also decodes correctly now.
- **Mouse camera: smooth while sailing** (#237). Turning the camera with the mouse on the boat (sailing, paddling,
  salvage) now follows the mouse directly at the same sensitivity as on foot, instead of turning in jerky steps.

- **Settings dialogs and phone text size.** Every dialog in Settings now has a close button in its title bar and also
  closes with a tap outside it, Esc, B or Back; long dialogs such as Gyro aiming keep Close on screen. On phones,
  Settings uses smaller text so more fits at once, while buttons stay at least 7 mm tall to tap. Tablets, handhelds
  and PCs look as before, and the UI size setting still wins.
- **Android: code mods** (such as the story quests). Turn on **Code mods** in the setup app (or Settings → Mods): your
  device builds the game again with mod support, and builds each code mod itself the first time it's used. Uses the
  compiler and Python the setup already has; the download grows by only a few KB.
- **Record graphics on a second thread** (Graphics → Advanced, off by default). The render thread hands recording and
  submitting the Vulkan commands to a second CPU thread. On a Xiaomi 14 it makes typical frames a little faster (about
  20% less render-thread time) but adds short hitches (the slowest 1% of frames about 3 ms slower), so it stays opt-in
  everywhere for now. **Steam Deck owners:** please try it on and off in the same spot and tell us the fps, the 1% lows
  and whether the Deck runs warmer.
- **Shader cache saved more often:** pipelines built while playing are now saved every few minutes and when the Android
  app goes to the background, so fewer are rebuilt after a crash or after Android closes the game.

- **World editor (preview, for mod makers):** start the game with `--editor` for a dedicated editor mode: after the
  title screen and file select it opens a free camera with its own panels (toolbar, outliner, inspector, problems &
  export, log). Place new actors by clicking into the picture, move and turn them with handles (grid and angle
  snapping), import `.glb` models with clear limit checks, write dialogue with a live preview in the game's message box
  and choose what happens after it, change everything with undo/redo, warp to any island, room or quest place, check
  the package with a clickable problem list and export it. **Play / Stop** lets you test as Link in a test mode that
  never touches your real save, and Stop puts the world back. New enemies, pots, switches and doors appear after a
  restart. Saving writes only your mod's own package. See docs/mod-editor.md.

- **World editor: Claude and other MCP clients** (for mod makers). In `--editor` mode, Settings → Editor → **Claude /
  MCP access** lets Claude Code (or another MCP client) use the editor's commands: place and change actors, write
  dialogue, warp, take screenshots and check the package. It is off at every start, only reachable from your own
  computer with a new token each session, and removing, replacing or exporting asks you to **Allow** in the game first.
  See docs/mod-editor.md.

- **World editor: visual quest editor** (for mod makers). Build a quest as a graph: steps are nodes, and dragging
  from one step to the next chooses who moves the quest on; branches, unreachable steps and other problems show on
  the nodes. **Start at this step** jumps straight into a test of any step (test mode, only your mod's own progress
  is set), the quest step and your mod's flags show live while you play, and you can search, select several actors
  and copy/paste actors and steps, also into another room.

- **World editor: live handles, docking panels, Esc** (for mod makers). Dragging a move or turn handle now moves the
  actor (or the whole selection) live in the game, still as one Undo step. The editor's panels dock to the screen edges
  and into each other as tabs, with a default layout and **Reset layout**. While placing, dragging or connecting quest
  steps, the first Esc cancels that; Esc with nothing to cancel opens Settings.

- **Mod SDK v3 preview: enemies, props, switches and effects** (for mod makers). Mods can now place the game's own
  residents, breakable props, switches and doors, and enemies with their own state; original mod enemies take damage,
  react to hits, can be targeted, defeated and drop items; mod props can be carried and thrown, and original item models
  can sit in Link's hand; plus a curated set of the game's particle effects, simple mod effects and custom textures on
  placed props. New original example: a patrolling enemy.

- **Android two-screen handhelds:** with GamePad screen set to Off, the game no longer takes the lower screen for a moment when it starts (#222).
- **PowerVR phones (e.g. Pixel 10): no more stretched spikes** (#207). On PowerVR GPUs the game now converts vertex
  data on the CPU automatically, because their drivers decode some of it wrongly in the shader. Only the one kind of
  data they get wrong is converted (found with a tester's help), so it costs much less speed than converting all of it;
  **Graphics → Advanced → Convert vertices on the CPU** switches it off (Automatic restores it).
- **Shortcuts: remapping works, Esc opens Settings** (#220). Remapped shortcuts take effect again (the Controls tab was
  catching the input first). Every binding clears the same way in Controls and Shortcuts (right-click, or Delete /
  Backspace / X on a focused binding); extra controller buttons (Capture/Share, rear paddles, touchpad click) can be
  bound; Home is listed under Settings. **Esc** now opens Settings (with a captured mouse the first Esc frees it), F1
  keeps working for now, and on macOS Cmd+, still opens Settings from the app menu.

- **Steam Deck Gaming Mode: sharp picture by default** (#214). In Gaming Mode the game now starts in full screen with the
  GamePad picture as picture-in-picture, because Gaming Mode shows one window at a time and a separate GamePad window
  made the picture blurry. Also with the Windows build through Proton. Your own saved choices and launch options still
  win; the old launch-option workaround is no longer needed.

- **Sploosh Kaboom: no more doubled hit and miss markers at higher frame rates** (#179). The board's markers are moved
  between cells once per game step; the in-between frames no longer blend a marker from its old cell to its new one.

- **Option to turn off the letterbox bars while targeting** (#114). Settings → Mods → Built-in → Camera →
  **Letterbox while targeting** (on by default). Switched off, normal Z-targeting no longer shows the black bars;
  cutscenes, scripted cameras, minigames and item aiming keep theirs.

- **Run/swim speed: boost bar options and "Roll, then hold A"** (#233). New **Show boost bar** switch; with Boost time
  set to no limit the bar no longer appears; the boost bar and the climbing stamina wheel now hide while menus, cutscenes
  and conversations are on screen. New boost button **Roll, then hold A**: roll while running and keep A held to boost
  until you let go (on land).
- **Android: big update for phones and handhelds** (most ideas from GreenNaugahyde's Android fork):
  - **Fewer stutters after Android closes the game:** the shader cache is now also saved every few minutes and when the
    app goes to the background, so the next start doesn't rebuild what you already had (desktop benefits after a crash
    too).
  - **Display refresh rate:** the game asks Android for the rate it draws (60, or 120 with 120 fps interpolation). Some
    phones (for example Xiaomi with HyperOS) still limit games to 60 Hz on their own.
  - **If the graphics driver crashes,** the game offers another driver on the next start; **crash reports** now also
    cover times Android closed the game itself, and include more detail.
  - **Older Qualcomm (Adreno) drivers:** shader pipelines are built one at a time there, which avoids crashes on those
    drivers.
  - **Less memory:** shaders keep less data after compiling, and save states are written and read in small pieces
    instead of all at once (all platforms).
  - **Gyro with the phone's own sensors:** Controls → Gyro… → Source: **Device gyro**.
  - **Phone vibration** follows the game's rumble while you use touch controls (Settings → Controls → **Phone
    vibration**: Off, Touch only, or also when no rumble controller is connected).
  - **Touch layout editor:** hold the on-screen controller button to move and resize the touch controls.
- **Save states: Last session and auto saves** (all platforms). A **Last session** state is kept when you quit (on
  Android also when the app goes to the background); **At start → Continue last session** in the Saves tab picks up
  right where you left off. **Auto save** every 2, 5 or 10 minutes (off by default) into three rotating slots. These are
  save states, separate from the game's own save file (#234).
- **New built-in mods** (Settings → Mods → Built-in, off by default): **Faster ivy climbing**, **Faster ledge hanging**,
  and **Jump anywhere** (L by default, optional double jump). **Cheats** gain infinite rupees, infinite bait and spoils,
  infinite swimming and "Fill the bags".

  **Please help test these:** we've checked the basics on our own phone, but these need real players and other
  devices. If something doesn't work, open an issue with your device and the log (Settings → Graphics → Bug reports →
  **Share log** on Android):
  - auto saves and **Continue last session** after quitting or switching apps;
  - **Device gyro** aiming and **Phone vibration**;
  - the **touch layout editor**;
  - **Jump anywhere**, faster climbing/ledge hanging and the new cheats;
  - phones with **older Adreno** or other GPUs, and whether 120 fps reaches your display.
- **True 60: no more crash at Orca's sword training** (#223). Talking to Orca and choosing "yes" stopped the game in True 60
  mode; event commands now only run on full game steps.

- **GamePad sounds through the main speakers** (#218). Sounds the Wii U plays only on the GamePad's own speaker (switching
  items, saving and other GamePad menu sounds) are now heard during TV play too. Settings → Audio has the switch (on by
  default) and its own volume; sounds already on the TV aren't doubled, and Off-TV Play is unchanged.
- **Linux AppImage: code mods work again** (#230). Turning on a code mod such as the story quests failed with "Guest mod
  build tools are missing". Setups made with an earlier start of the AppImage are repaired automatically, without running
  setup again.

- **Settings: Quit Game** (#231). A button at the bottom right of every Settings tab, with a confirmation (Cancel is
  preselected), so you can quit with just a controller, e.g. on a Steam Deck in Gaming Mode or an Android handheld. On
  Android the game also leaves the recent apps list.
- **Graphics → Advanced, diagnostic options for broken 3D geometry** (e.g. PowerVR GPUs, #207): "Convert vertices on
  the CPU", "Full precision in shaders" and "No vertex reuse", each off by default and applied after a restart. They help
  us find the cause on GPUs we can't test ourselves; the first one is slow and only meant for testing.
- **Mod SDK v3 preview: animated characters and scenes.** Mods can add animated characters (a rigged glTF model
  with idle, walk, talk and extra clips) that look at Link, walk paths or to quest places, block Link and react to
  sword hits; mod code can drive them through a small C API. Scenes gain eased camera paths, follow shots, music,
  and the game's choice box with branches. Two original examples: a tide sprite on Windfall and a wandering creature
  (preview, for mod makers).
- **Mod SDK v3 preview: static mod models.** A quest mod can now place its own static model (a glTF `.glb` from
  Blender) in the world, lit and shadowed like the game's objects, with collision, L-targeting and a Check/Talk
  prompt. The model is converted on your own machine using a material from your own game, so packages never contain
  game files. The Windfall example gained an original carved post (preview, for mod makers).

- **Android handhelds** (thanks @almost-drowned-rat):
  - **Full screen** now hides the system bars on Android 11 and newer too, so they no longer cover the top and bottom
    of the picture (based on GreenNaugahyde's Android fork; #212, #225).
  - The app's **files folder** (saves, settings, mods) appears in the system Files app and file picker; the prepared
    game and shader caches are read-only there and console keys are never shown (#213, #226).
  - The system **Back** button or gesture opens and closes Settings, like F1 on desktop (#215, #227).
- **Meta Quest: "Add the game icon" works** (#249, thanks @rhemfur): on Quest the button now pins a shortcut in the
  Library, and it says so when no icon can be added.

- **Render-thread fast path** (Vulkan): a draw reuses the previous draw's prepared state when only pointers or
  arithmetic values change, about 6–9% less render-thread CPU in tests (most on devices where the render thread limits
  the frame rate, like the Steam Deck), with identical pictures. On by default; `WWHD_VK_FASTPATH=0` turns it off if
  you see wrong textures or blending, and please report it (#178).

- **Android two-screen handhelds: GamePad screen Off frees the lower screen** (#222). With the GamePad screen switched off,
  the lower screen is released to the launcher and other apps (e.g. a video on an AYN Thor); switching it back on takes it
  again with picture and touch.
- **Mods on Windows:** a mod's preparation tool can now load its own helper files there too (the port's Python on
  Windows didn't look next to the tool). This stopped the GC minimap from installing on Windows; gc-minimap 0.2.2
  also fixes it on its own for v0.3.0 (#224).

- **Settings → Shortcuts:** remap the app's shortcuts (Settings, full screen, screenshot, save and load states, the
  graphics toggles), each with a key, a second key and a controller combination such as Select + D-pad Up. New
  actions (GamePad picture-in-picture, performance overlay, mute, volume) start unbound, and **F6** now loads state
  slot 1. The defaults stay as they were (#220).
- **macOS setup:** when macOS points to another developer folder than the installed Command Line Tools (a removed
  Xcode, or one whose license hasn't been accepted), setup now uses the installed tools directly instead of waiting
  forever, and says what to do if they still can't be used (#219).

- **Android, two screens (AYN Thor and similar):** choosing GamePad screen **Off** (or hiding it) while the second
  screen is connected now turns that screen black and ignores its touches while the game plays on the main screen;
  the choice is remembered. Without that choice the second screen shows the GamePad as before (#222).

- **GamePad picture-in-picture placement:** nine positions (corners, edge centres, centre) plus a custom one,
  exact size (10–50%) and opacity (20–100%), and a small screen diagram in Settings → Display where you tap or drag
  the GamePad picture into place; while that section is open the game shows a live, draggable preview. The default
  bottom-right position now sits above the rupee counter instead of covering it (#221).
- **Mod SDK v3 preview (opt-in, for mod makers):** mods can place a small set of the game's own actors (chests,
  signs, a Windfall resident) with talk actions, and play short scenes: camera shots, dialogue, fades and the game's
  letterbox bars, skippable with Plus/Start. Placed chests and signs keep their state in the mod's own part of the
  save. Nothing changes without such a mod. See docs/mod-sdk-v3.md and the sdk-v3-windfall example.

- **Android: "Share log"** in the setup app and in Graphics → Bug reports: it shares the latest game logs through
  Android's share sheet, with your user paths removed (as in crash reports). Android now writes a log by default
  (at most 64 MB per run), so there is one to share after a problem. The log also lists more about the GPU (driver,
  vertex formats, limits), and Graphics → Advanced has **Force RGB vertex fallback** for testing GPUs like PowerVR
  (#207).
- **Intel graphics on Windows:** drivers older than 101.5000 now use the classic render-pass mode automatically,
  which fixes the crash at start on driver 101.3111 (Iris Xe) without setting anything (#204).

## v0.3.0

- **Controls: Xbox and PlayStation button prompts also with "By position"** (issue #209, thanks @jamescastells):
  with the Nintendo button layout on an Xbox or PlayStation controller, the game's button icons now show the button
  that is physically in that place (for example the roll as a red B and the sword as a green A on an Xbox pad).
  Custom face-button mappings show the buttons they are bound to. The settings' help texts now explain the two
  options more clearly. "By label" and "Original" look as before.

- **Vulkan: a fallback for GPUs without 3-part vertex formats** (issue #207, Pixel 10 Pro with a PowerVR GPU): some
  GPUs can't read three-component vertex data directly, which showed as "exploded" shapes and a black screen. The
  game now checks this at start and reads such data in two parts on those GPUs. Every other GPU keeps exactly the
  same shaders and caches as before.

- **Fixed: loading a save state switched the controller back** to the one the state was made with (for example a
  GamePad state loaded while playing with a Pro Controller). Save states now keep your current controller, and the
  game follows it right away, without any menus: after loading a full state, or after switching the controller in
  the Input menu during play, Link moves with the controller you use and the button prompts match.
- **Fixed: after loading a save state, story mod pages fell back to their plain look** (for example the Story
  Notebook's page). Their layouts are now built again right after the load.

- **Fixed: a story mod's treasure chart showed its X outside the map.** The chart view shows a small window of the
  sea square; a mod chart now uses the window that contains its treasure spot, so the X sits on the map at the spot,
  on the TV and the GamePad.

- **Linux: sharp picture with desktop scaling on Wayland** (issue #201, thanks @findel77): the game window now
  uses the screen's real pixel size, so with display scaling (for example 4K at 250%) the picture and the
  settings overlay are no longer rendered small and stretched. On older versions, `SDL_VIDEO_WAYLAND_SCALE_TO_DISPLAY=1`
  works around it.

- **Mods: development builds can install packages for the next release.** A development build counts as the next
  version for the mod catalogue (for example v0.2.14 plus newer changes counts as 0.2.15), so packages that need
  the coming release, like the Story Notebook 0.6.1, install on it. Releases are unchanged.

- **Extended enemy lock-on** (#192, PR #193, thanks @mhbxyz): Mods → Extended enemy lock-on, off by default. The
  enemy lock-on range can be raised to 1x–3x (saved per profile), and R3 while locked on to an enemy switches to the
  next eligible enemy, wrapping around. The game's own targeting still decides who can be targeted; talking
  distances to characters are unchanged.

- **For porters: more overlay switches** (PR #205, thanks @Pulpparty): a port to another platform can now turn off
  code mods, mod downloads, the Speakers choice and the keyboard hints in the settings menu (`overlay::ext::Features`).
  Nothing changes on Windows, macOS, Linux or Android.

- **Fixed: ropes jittered with frame interpolation** (issue #202, thanks @floysh and @JulianSK1999): the grappling
  hook's rope in Link's hand and rope bridges (for example on Outset Island in the title scene) were drawn at the exact
  game step while everything around them was drawn in between, so they jumped back and forth at 60, 120 or 240 fps.
  They now move smoothly with the rest of the picture. 30 fps and true 60 are unchanged.

- **Cheats: moon jump** (idea from the GreenNaugahyde Android fork): in Gameplay → Cheats, off by default. Hold L
  to rise; the left stick steers relative to the camera. **No fall damage after moon jump** is a separate option.
  Works at 30 fps, with frame interpolation and in true 60; loading a state ends a flight.

- **Vulkan: textures a graphics driver cannot filter smoothly** (some Android GPUs) are now sampled sharply
  instead of breaking Vulkan's rules. Every other device keeps the same filtering as before.

- **Fixed: leftover memory in the save file's empty Quest Logs.** With story mods on, saving could copy leftover
  bytes into unused padding of the empty Quest Log slots. Those bytes are now always zero; the game never reads
  them, and the active Quest Log is unchanged.

- **Mod SDK v2: items and treasure charts for story mods** (for mod makers; nothing changes without such a mod):
  - **Mod items:** mods can declare their own items with names, descriptions, icons in the game's item pictures
    and stacks. Link keeps them in the Delivery Bag on extra pages, can put them in the bag's own places, and can
    put them, or the story notebook, on X, Y or R to use them. While Link has mod items, R in the Delivery Bag
    turns to the extra pages (L/R turn pages) instead of putting an item on R; X and Y still assign items.
  - **Treasure charts:** a mod's treasure chart now works like the game's own: the light ring at its spot, the
    crane, the chest and its item (a game item or a mod item), then the chart counts as finished.
  - **Saves:** mod items and charts are kept in the story mods' own part of the save, and turning the mods off
    shows the game's inventory exactly as before.

- **Story Notebook: a new look for mod quests** (Mod SDK v2): the notebook's list and quest pages are now drawn
  as real game layouts, on the TV and the GamePad, with a mini sea chart on each quest page, the open book in the
  Delivery Bag and the game's own fonts. A quest's page says "Not started yet" with its first place, "Finished"
  when done, and offers "Show on chart" only for a step with a place on the sea chart. For mod makers: mods can
  give their items their own icons in the game's item pictures (built from your own game files on your machine).

- **Android: system game modes** (idea from the GreenNaugahyde Android fork): the game declares Android's
  performance and battery-saving game modes, and the system no longer downscales the picture or overrides the
  frame rate, so the port's own resolution and frame pacing stay in charge.

- **Vulkan: a classic render-pass mode for older graphics drivers** (issues #144 and #51): the game now also runs
  on Vulkan 1.1 / 1.2 drivers without dynamic rendering (for example older AMD drivers), and older Intel
  Windows drivers (before 101.2121), which crashed on dynamic rendering, use the classic mode automatically.
  `WWHD_VK_FORCE_RENDERPASS=1` forces it for testing; `=0` turns it off. Everyone else keeps the current
  renderer path, with the same pictures and performance.

- **Fixed: Windows setup stopped while linking when the folder name had an emoji or other special characters**
  ("'charmap' codec can't encode character"). Setup now writes its linker file lists as UTF-8, so the game can be
  set up in any folder.

- **A new app icon** (issue #199, thanks @thorazine111-coder): a sailing boat on the sea, original artwork, on
  Android (adaptive icon on Android 8+) and in the Linux AppImage. The game's own icon from your copy is still
  available as a home-screen shortcut (Android setup: **Add the game icon to the home screen**).

- **Story system for quest mods** (letters, a quest notebook and sea-chart marks, in the game's own style). The
  port brings the system; all quest content comes from the mod catalogue:
  - **Story system** (Mods > Built-in, on): it needs the **Story Notebook** package from the mod catalogue, which
    setup installs (it needs the internet once). If setup was offline, install it later in Mods: switch the
    Story system on, or press Install on its line. It's required for every quest mod and stays enabled. The game's
    start never goes online; without the package the story system stays inactive and the normal game is
    unchanged.
  - **Letters by Rito mail:** a quest mod announces its quest with a letter from the Rito postbox, once Link has
    the Delivery Bag. The Story Notebook package's letter brings the **story notebook**.
  - **Story notebook:** in the Delivery Bag. Its page lists every mod quest with the next step, where to go and its
    status, with an Archive tab for finished quests. "Show on chart" puts the game's red circle on the tracked
    step, also in the zoomed chart square, and it follows characters who walk around.
  - **Quests described as data:** a mod's `manifest.json` lists its quests, steps, places, characters and rewards.
    Steps can wait for story or item conditions ("Not yet possible: ...").
  - **Characters take part:** a quest can give a resident new lines. When several quests want the same character,
    the game's own choice box asks which one. The King of Red Lions says a one-time line for the tracked quest.
    Residents are named by neutral ids; their names and places are read from your own game files.
  - **Mod charts:** a quest can hand Link its own chart (by letter, in a conversation or as a reward). It appears
    in the MAP menu's chart list after the game's charts, opens in the game's own chart view with an X at its
    place, and gets the check mark when its treasure is found. "Open chart" in the notebook jumps there. Mod
    charts have their own numbers after the game's 61 charts and never touch the game's charts or save data.
  - **Progress is kept in the save file** in a small block the game itself ignores. Removing the mods leaves a normal
    save.
  Details for mod authors in docs/mod-sdk-v2.md; the Story Notebook package and the example quest mods are in the
  mod repository.

## v0.2.14

- **The controller selection after the title screen is skipped** (issue #189, thanks @floysh): the game no longer asks
  whether you play with the Wii U GamePad or a Pro Controller, because the port already knows it from Controls. It picks
  the matching option through the game's own selection, so everything that depends on it stays the same. To choose by
  hand again, turn off **Controls > Skip controller selection**.

- **GamePad screen "Automatic": follows the game's menus** (from a play test): in the automatic GamePad screen mode,
  the GamePad picture now appears in the corner of the TV whenever the game opens something on the GamePad (items,
  maps, the collection, the Delivery Bag, save and options, song and warp lists) and goes away shortly after it closes.
  Before, it guessed from how much the GamePad picture changed and often missed the pause menu. Cmd+G still keeps
  it up, and Off-TV Play shows the GamePad picture full size as before. The other GamePad screen modes are unchanged.

- **PlayStation and Xbox button looks in the action hint near Link** (issue #188, thanks @floysh): the floating hint that
  appears next to Link (for example "Speak" in front of a character) still showed the A button with PlayStation or
  Xbox button labels. It now uses the same shapes and colours as the button cluster in the corner. With the original
  look nothing changes.

- **Experimental stereo 3D, HDR output and a gameplay FOV setting** (PR #168, thanks @pancreations): the Graphics tab
  gains **Stereoscopic 3D** (Vulkan only: Half SBS, Full SBS and top-and-bottom, with depth, convergence and eye swap;
  each eye renders its own view; 30 fps recommended for now), **HDR output** (Windows with Vulkan, when Windows HDR is
  on; falls back to SDR with a note if the display can't do it) and **Gameplay FOV** (75% to 250%; cutscenes, dialogue
  and menus keep their original view). All three are off / 100% by default, and the game looks and runs exactly as
  before until you turn them on. Settings are kept between launches, on macOS too.

- **Ports can bring their own renderer** (PR #187, issue #185, thanks @Pulpparty): a platform port (for example a
  Switch build with deko3d) can now register its own renderer at start-up (`gfx/renderer_ext.h`) without editing the
  renderer or GX2 code. Nothing changes for Metal and Vulkan: the renderer choice, menus, shader caches and
  performance stay as they were.

- **Mouse buttons and the wheel can be bound in Controls** (issue #165, thanks @PNOAGAMER): left, right, middle, side
  buttons 4/5 and wheel up/down are now bindable like keys, so the game can be played fully with mouse and keyboard
  together with the mouse camera (Mods > Built-in). Bound mouse buttons drive the game while the mouse camera has
  captured the pointer in the TV window; the settings menu, GamePad-window touch and text prompts keep the mouse as
  before, and "first person on the mouse wheel" keeps priority over wheel bindings. Nothing is bound by default.

- **Setup: disc images and Cemu's keys.txt** (from a player support case): a `.wux`/`.wud` needs two keys, the disc key
  of your dump and the Wii U common key. Setup now accepts Cemu's `keys.txt` directly and finds both keys in it
  (choose it once for either key), the disc key can also be pasted, and the keys screen explains this, including
  the no-key alternative (Cemu's "Convert to compressed Wii U archive (.wua)"). Clearer errors, e.g. when the
  common key was entered as the disc key. Key values are never shown, logged or saved. (Desktop setup; Android follows.)

- **Android setup: keys.txt and safer key handling:** the same `keys.txt` support as on desktop (choose Cemu's
  `keys.txt` once, setup finds both keys). Key files are now only read when needed and no longer copied into the app;
  if a key file was moved or its access was lost, setup asks you to choose it again instead of failing. Setups
  started with an older version keep working.

- **Less memory per shader translation** (PR #184, thanks @Pulpparty): the shader translator reserves 1 MB of text
  per shader instead of 12 MB and grows only if needed (the largest game shader is far smaller), which helps devices
  with little memory. One of its text functions also no longer cuts long text short.

- **Building from a folder with spaces in its path** (PR #183, thanks @Pulpparty): the shader translator and two tests no
  longer fail to compile when the checkout path contains a space (for example a Windows user name like `C:/Users/Jane Doe`).

- **macOS: Nintendo controllers have the right buttons** (issue #181, thanks @Nickinfinity): with a Switch Pro Controller,
  Joy-Cons or an SNES-style pad, **By position (Nintendo)** had A/B and X/Y swapped, because macOS reports these pads by
  their printed letters. They now drive the right buttons and show Nintendo icons; a single sideways Joy-Con is handled
  too. If you swapped A/B and X/Y by hand as a workaround, choose **Input > Face buttons > By position (Nintendo)** or
  **Automatic** once after updating; your saved bindings are otherwise kept.

- **Steam Deck Gaming Mode: the settings menu stays open** (issue #112, thanks @chaberl-art): with the TV and GamePad
  screens in two windows, the menu could close again right after opening. F1 now toggles once per press (a release
  is needed before it toggles again), keys and the save-state hotkeys (Shift+F1..F5 save, F2..F5 load) work from
  either game window, and switching focus between them clears held keys without closing the menu. The log now says
  why the menu opened or closed (`[overlay] closed (F1 key, window TV, ...)`). In Gaming Mode the menu, the save-state
  messages and touch now follow the window that has focus, so they also show when the GamePad window is the one on
  screen (before, the menu could open there invisibly and hold Link in place).

- **Settings overlay: extension points for other platforms** (PR #175, thanks @Pulpparty): ports such as the
  community Switch port can add their own settings tabs, hide built-in ones, plug in a system keyboard, controller
  shortcuts and on-screen layers, without changing the overlay's code. Nothing changes on our platforms.

- **The aiming reticle no longer doubles at 60+ fps** (issue #163, thanks @Julancho): with the boomerang, grappling
  hook and hookshot, the red aiming dot (and the boomerang's target markers) stayed at the game's 30 Hz position
  while the camera moved smoothly between frames, so it jumped back and forth during camera turns. It now moves
  with the camera; 30 fps is unchanged. `WWHD_RETICLE_FIX=0` turns the fix off.

- **No crash when memory for a shader's source runs out** (PR #171, thanks @Pulpparty): the shader translator's text
  buffer now handles failed allocations (the shader just fails to compile and is retried) instead of writing through
  a null pointer. Mostly matters on devices with little memory.

- **Android: setup works again on the development build.** Since the decomp version arrived in setup, the app's
  setup stopped right away ("No module named 'decomp_setup'"); the missing file is now packed with it. Also clearer
  game-icon hints: "already on the home screen" for the plain icon too, and wording that fits every launcher
  (PR #170, thanks @rhemfur).

- **Android setup polish** (from the tester report on PR #98, thanks @rhemfur): the "Rebuild shader cache" hint
  names the right button on each platform (on Android: *Start / resume / retry setup*); tooltips no longer stay on
  touch screens; setup's own step reads "Reading shaders from your game files" so it doesn't look skippable, and its
  notification brings setup back to the front; after an app update, setup shows "Updating the game for the new version".

- **Fixed: the boat barely turned with "Sailing speed" on** (issue #169). The game turns the boat more slowly the
  faster it sails, so the boosted speed hit its minimum turn rate. The rudder now responds like at normal speed for
  both sails, while the boat keeps its boosted speed; automatic wind and the jump height are unchanged.

- **Game code built with `-fwrapv`** (PR #172, thanks @Pulpparty): PowerPC integer arithmetic wraps around, and the
  compiler now guarantees the same for the recompiled game code, the decomp version and the verify tools, instead of
  relying on the code never overflowing. No visible change.

- **Fixed: "Invert camera up/down" also inverted the Wind Waker baton** (issue #167). The setting now applies
  only to the camera, including first-person views and item aiming (bow, hookshot, telescope, Picto Box) and the
  mouse camera (which now follows it too); the baton and other right-stick controls keep their normal direction.

- **Fixed: stutter at 240 fps and a slow start with "Keep game speed"** (issue #164). Since v0.2.13 the game's
  30 logic steps drifted against the display (59.94 Hz), so every few seconds the in-between frames bunched up and
  the picture hitched; the steps now follow the display's refresh. A hitch right after starting could also keep
  all in-between frames off (30 fps) until the frame rate was changed; that measurement is now retried. Slower PCs
  keep v0.2.13's smoother pacing (#125).

- **Mods tab: Built-in and Community.** The Mods list is split into *Built-in* (the options that come
  with the game, such as the camera and speed options) and *Community* (mods from the mod catalogue,
  installed or ready to download, plus imported packages and graphics packs); *More* is unchanged.
  Mods now use the game ID `wwhd` for both regions; older manifests with `wwhd-usa` keep working.
- **Fixed: Call of the Sky could not be switched on from the catalogue.** After confirming the
  experimental warning, the install stopped with "confirm the warning first", because the save backup
  was skipped. The backup now always runs once before the mod is switched on, and a failed backup shows
  its reason.
- **Fixed: a red "conflicting ID" box in Mods › More** (Code-mod support). Release builds also no longer
  show this debug highlight.

- **Fixed: controllers stopped responding in the game after the settings overlay or the name screen**
  (issue #153). If a trigger or stick did not fully return to rest, the game ignored all input after
  closing the overlay or entering a name until the controller was reconnected. Now only the button that
  closed it waits for its release; everything else reaches the game at once.

- **Linux AppImage: portable use now uses the AppImage's standard portable mode** (issue #55). Start
  the AppImage once with `--appimage-portable-home`, or create a folder named like it plus `.home` next
  to it (e.g. `WindWakerHD-…-linux-x86_64.AppImage.home`); the game, its code, saves, settings and caches
  then stay in that folder. **If you used the `….AppImage.data` folder from v0.2.13**, it is no longer
  used; to keep your saves and settings, move its contents before starting the new version:
  `….AppImage.data/share` → `….AppImage.home/.local/share`, `….AppImage.data/config` →
  `….AppImage.home/.config` and `….AppImage.data/cache` → `….AppImage.home/.cache`, then start the
  AppImage once with `--setup`.

- **Share your shader coverage:** Settings › Graphics › **Export shader template** (desktop) or
  `tools/shaderprep.py export` saves a code-free `my-template.bin` (shader hashes and GPU settings only, no
  game code) from your own shader caches, so it can be contributed to the template that setup uses to
  prepare shaders. `shaderprep.py template` no longer fails with "invalid recipe bounds" on long-played or
  Vulkan caches and skips damaged records instead of stopping.

- **European GameCube saves can be converted to HD** (`tools/savegame/gc2hd.py`): PAL saves (`GZLP01`)
  are accepted, names with accents (and Œ/œ) come through, and the save's language is kept. The
  converted `cking.sav` works with both the USA and the European HD game.
- **Android handhelds and controllers** (issue #152, thanks @OTPR26):
  - **Settings with a controller:** holding Select/− now opens the settings after half a second as
    intended (it could take about 3 seconds on some devices), and a one-time hint says so.
  - **Touch buttons hide with a controller:** the view and 🎮 buttons hide while a controller is
    connected (built-in or external) and come back for a few seconds with a tap in their corner.
    Settings › Display › *Touch buttons*: Automatic / Always show / Hide.
  - **AYN Thor / Odin built-in controls:** A, B, X and Y now sit in their Nintendo positions, all four
    can be bound in Controls, and empty bindings in `controls.json` are repaired.
  - **Larger settings on phones and handhelds:** the settings fill most of the screen with bigger
    buttons, sized for the screen; Settings › Display › *UI size* (Automatic / Small / Medium / Large).
    The desktop look is unchanged.
  - Texts say "Android device" instead of "phone", and the log names how Android reports a controller.

- **Fixed: Windows setup failed in deep folders** (issue #144), for example in Downloads after Windows'
  "Extract All" puts the folder inside a folder of the same name. The compiler now unpacks with long
  paths, and the game and setup use much shorter names for shader caches, temporary files, code-mod
  builds and save backups (existing caches are reused or rebuilt automatically). Only a folder deeper
  than 175 characters makes setup stop right away, before downloading anything, and ask you to move it
  to a shorter place such as `C:\Games\WindWakerHD`.
- **Development builds (macOS): setup can also build the decomp version** (experimental, off by
  default). A checkbox in setup (or `--decomp`) builds, next to the normal game, a version in which the
  game functions of the public decompilation run as compiled source; it is built from your own game
  on your Mac, and the normal game is always built first and stays the default. Choose which one starts
  in setup, or start Wind Waker HD with `--decomp`. Setup downloads the decompilation's source (about
  8 MB, only when the option is on) from its GitHub repository and checks it before use; the release
  itself does not contain it. The decomp build now uses the readable decompilation; its model data is
  generated from your game at build time. Regular releases are unchanged.

- **Fixed: unreadable shoulder labels on the HUD** (issue #143). With *L1 R1 L2 R2* or *LB RB LT RT* as
  shoulder names, the labels on the HUD's shoulder buttons were white on the white plates; they are now dark
  like the game's own letters again (and dim the same way). Dialog labels are unchanged.
- **Mods tab redesign.** Every mod has an on/off switch in one list, including mods you haven't
  downloaded yet: switching one on downloads it, sets it up and turns it on. When something is needed,
  one review asks it all at once (your GameCube game, choices, code-mod support, the experimental
  warning), then one progress view follows and keeps running while you play. Mod pages explain in one
  line why something can't change right now instead of showing greyed buttons; updating keeps a mod on
  or off as it was; Remove also deletes a mod's prepared files and says how much space that frees. The
  mod list loads by itself (cached, checked in the background; only the public list is fetched). Rarely
  used things (profiles, install from a file, GameCube game, code-mod support, save backups, mod list
  source, cheats) are under Mods › More.
- **Experimental: build the game from the decompilation** (macOS, for developers). The CMake option
  `WWHD_DECOMP=ON` compiles the game code from the public, verified decompilation
  ([ZeldaWWHDDecomp/wwhd](https://github.com/ZeldaWWHDDecomp/wwhd), a submodule in `decomp/`) instead of
  recompiling it; the libraries stay recompiled from your game. It plays identically to the normal build
  but still uses more CPU, so releases stay recompiled. Off by default: normal builds are unchanged. See
  docs/decomp-build.md.
- **Fixed: aspect ratios other than 16:9** (PR #146, thanks @fluxehub). At 16:10 (and 4:3) the
  extra height now shows more of the view above and below instead of cropping a 16:9 picture, and
  the HUD sits at the real screen edges. At 21:9 and 32:9 the button cluster and the Wind Waker's
  "Back" prompt keep the same distance from the edge as at 16:9, and the Wind's Requiem screen
  fills the whole screen instead of showing dark side bars. Layouts drawn to both the TV and the
  GamePad no longer flicker between the two aspect ratios, and Vulkan no longer leaves the last
  pixel row black at 16:10 with 1.5x. 16:9 is unchanged.
- **Android: a menu on the game widget and an "Add the game icon" button** (PR #150, thanks
  @rhemfur). Long-press the home-screen widget, then Settings (Samsung) to export or import the save
  or open the game setup. The setup screen has **Add the game icon to the home screen** under Play
  for players who declined or removed it, and the automatic offer now waits until the setup screen
  is in front, so Android no longer refuses it on some phones. Spanish texts for these and the save
  export/import.

- **New gameplay mods: faster ladders, faster sidling, sailing speed and automatic wind** (issues #95,
  #96; all off by default, Mods tab and the macOS Gameplay menu). Ladders and sidling play faster with
  the animation in step (1–3×); sailing speed has separate settings for the normal sail and the Swift
  Sail; *Automatic wind* makes the normal sail behave like the Swift Sail (the wind follows the boat).
  With the mods off the game is unchanged.
- **Fixed: ZL/ZR (LT/RT, L2/R2) prompts in dialogs showed as a white box** with the face buttons set
  to *by label* (issue #141). The shoulder labels in dialogs are now drawn as pictures like the
  game's own icons, in every language.
- **Button prompt options** (issue #143, Controls tab of the settings overlay and the macOS Controls
  window): *Button prompts* Automatic / Original / Xbox / PlayStation (Automatic shows PlayStation
  shapes on a PlayStation pad), *Shoulder names* Automatic / L R ZL ZR / L1 R1 L2 R2 / LB RB LT RT,
  and *Coloured face buttons* (on by default; off keeps the game's white buttons). The PlayStation
  shapes are drawn by the port, no images are included. Unassigned (dimmed) HUD buttons now keep
  their shape visible, and the prompt strokes are a little bolder.
## v0.2.13

- **Linux AppImage: a portable mode** (issue #55, idea by @missionfloyd). Create a folder named like the
  AppImage plus `.data` next to it (e.g. `WindWakerHD-…-linux-x86_64.AppImage.data`), and the AppImage keeps
  the game, its code, saves, settings and caches there instead of in `~/.local/share`, `~/.config` and
  `~/.cache`. Without that folder nothing changes.
- **Profiling on Windows on ARM64: a sampling profiler for the frame threads** (PR #147, thanks
  @rhemfur). Off unless `WWHD_SAMPLE_THREADS=<file prefix>` is set; `tools/bench/thread_profile.py`
  turns the samples into a report by module and function, including time inside the Vulkan driver,
  without administrator rights. See docs/performance.md.
- **Android: a friendlier setup screen, and the app icon starts the game** (PRs #128 and #129, thanks
  @rhemfur). Setup now guides you step by step in English, Portuguese or Spanish ("Choose your
  game", "Preparing the game for your phone… 20 of 80 · about 3 min left", "Your game is ready").
  Once a game is set up, the app icon starts it directly ("Game setup" stays available by
  long-pressing the icon), and the phone offers a home-screen game icon with the game's own picture,
  made from your game files.
- **Fixed: crash with Quick doors when leaving a room with spores** (issue #145), for example at
  the first door in the Forbidden Woods. Quick doors ran the spores' fade-out faster than the game
  draws, so their effect was removed while still queued for drawing. Doors now wait for the
  weather effect to finish fading (rain, stars, spores, fog and waves) and stay fast otherwise.

- **Windows, Intel UHD graphics: a workaround for a crash in Intel's Vulkan driver** a few seconds
  after start (issue #144). On Intel GPUs every render-target view now states its purpose explicitly;
  `WWHD_VK_ATTACHMENT_VIEW_USAGE=0` turns it off, and `WWHD_VK_TRACE_PASSES=1` logs each render
  pass once for crash reports.

- **Fixed: shadows switching between sharp and blurry at high internal resolutions** (issue #135).
  Above 1x, the sun-shadow pass rebuilt surface normals from rays one console pixel apart but depths
  one screen pixel apart, so depending on the camera angle shadows smeared and Link could get a dark
  outline. The pass now uses matching spacing, so shadows at 2x, 3x or 4K look like at 1x, only sharper.
- **Fixed: Android GPU crashes ("device lost") on some Adreno phones** (issue #104). The precise
  GPU barriers that v0.2.12 turned on for Android caused occasional crashes with Mesa Turnip on an
  Adreno 650 and gave no measurable speed-up, so they are off by default again on every platform
  (`WWHD_VK_NARROW_BARRIERS=1` still turns them on). Thanks to @rhemfur for the measurements.
- **Fixed: setup stopped with "the recompiler wrote no code"** (issue #136) when the install folder's
  path contains square brackets, for example `...\LoZ - The Wind Waker [WiiU]\data`. Setup and the
  recompiler treated the brackets as a search pattern and then found none of their own files.

- **Android setup: "Continue anyway" when it waits for the phone to cool down** (issue #142). The
  pause now shows the battery temperature and Android's thermal status, waits only at 45 °C battery
  temperature (42 °C if Android also reports the phone as warm) or when Android reports it as hot,
  and a **Continue anyway** button (also on the low-battery pause) lets you go on for this run. Only
  Android's critical heat level still stops setup.
- **Mods: experimental mods and save backups.** Mods marked *Experimental* (untested, may break the
  game or a save made while they are active; Call of the Sky is one) are hidden unless you turn on
  **Show experimental mods** in the Mods tab. Turning one on asks first and backs up your saves before
  it is enabled. **Mods → Save backups** lists the backups (the newest 10 automatic ones are kept;
  *Back up now* backups are never removed), can restore one at the next start, before the game loads
  any save (your current saves are backed up first, so a restore can be undone), or delete one.

- **Setup: "Preparing shaders" is much faster and shows real progress.** The release now includes the
  native decoder for the game's compressed archives (before, setup unpacked them in slow Python) and
  scans them on several cores: about 10 seconds instead of about 4 minutes on an Apple Silicon Mac,
  with an identical result. The bar now fills as it works ("400 of 818 game archives", then the
  shaders and pipelines compiled), and Vulkan compiles pipelines on several threads. On Android the
  app now ships the same native decoder, so preparation on the phone is much faster as well.

  shaders and pipelines compiled), and Vulkan compiles pipelines on several threads.

  On a Mac, setup now prepares both Metal and Vulkan, so either renderer starts without first-time
  shader stutter.
- **Controls: in-game button prompts follow your controller** (PR #110, thanks @mhbxyz). With Face
  buttons set to *by label* (or *Automatic* on an Xbox-style pad), dialog icons and HUD buttons use
  green A, red B, blue X and yellow Y, the HUD button cluster uses the Xbox positions, and shoulder
  prompts read R1/R2 and L1/L2 in the game's own lettering. *By position* keeps the original look.

- **Controls: a "Stick range" setting per stick** (issue #40). For a worn stick that can't reach full
  deflection: raise its range (100–150 %, default 100 %) in Settings → Controls or the macOS Controls
  window until the input reaches full tilt, for example the Wind Waker baton's directions. It's
  applied after the dead zone and keeps the stick's direction.

- **Mods: a "Restart required" popup.** Changes that only take effect after a restart (turning
  packages, graphics packs or code mods on or off, their settings, profiles, a finished one-click
  setup, a code-mod rebuild) now show a popup with **Restart now** and **Later**, and the Mods tab keeps
  a "Restart to apply" line until you restart.

- **Fixed: one-click mod setup stopped with "Package changed; refresh it and confirm its code again"**
  after a preparation step, e.g. for the GameCube sea minimap. The mod's Python tool left cache files
  in the package, which made the package look changed. Setup tools now run without writing them. If you
  already hit this, refresh the mod in the catalogue (or delete its `tools/__pycache__` folder) and
  press Try again.

- **Vulkan: choose your graphics card** in Settings → Graphics (issue #135), e.g. on laptops with an
  Intel and an NVIDIA GPU. The choice is remembered and applies after a restart; if the chosen card is
  no longer available, the game picks a usable one and says so. `WWHD_VK_DEVICE` (an index or part of
  the name) overrides it for one start.

- **Fixed: the 60, 120 and 240 fps settings dropped to about 30–50 fps on PCs that can't quite draw
  60 frames a second** (issue #125). A frame that misses its display refresh is now shown as soon as
  it's ready instead of waiting a whole extra refresh, so you get close to what Uncapped shows (e.g.
  56 instead of 30 fps). Frames that are on time are paced exactly as before.

- **Fixed: the Picto Box picture stayed blurry** (issue #134). The left stick zooms the Picto Box, and
  the game blurs the picture while zooming, so even a slightly drifting stick kept it blurry. Controller
  sticks now have a 15 % dead zone by default (Settings → Controls; a saved 0 from older versions
  becomes 15 %, an explicitly chosen 0 stays). On Metal, the photo's subject outline could also miss
  pixels the first time; that's fixed too.

- **Less stutter on first play: setup now prepares the shaders.** After building the game, setup runs
  **Preparing shaders**: from your own game files and a small, code-free list of the game's GPU states
  that comes with the release, it compiles the shaders and pipelines before you play (about 15–20 s on
  a recent Mac). In our tests the cold first-play hitches over 50 ms went from 5–10 to 0. You can skip
  it in setup, and Settings → Graphics → **Rebuild shader cache** prepares a fresh one. Areas that
  aren't covered can still stutter once. On Vulkan, shaders and pipelines are also compiled in the
  background while you play, and the pipelines you used are rebuilt at the title screen on the next
  start. Your existing caches are kept when you update.

- **macOS setup: works with mismatched Command Line Tools** (e.g. after a macOS beta update installed a
  newer SDK than the tools' linker understands: "unknown architecture arm64e.x1", "tapi error: malformed
  file"). Setup now links with an older installed SDK automatically, or explains how to update the
  Command Line Tools.

- **Audio: a master volume slider** (PR #137, thanks @mhbxyz, issue #124). Settings → Audio has a
  Master volume slider with Mute and Default buttons. It applies immediately and is remembered for the
  next start.

- **Fixed: Link flickered during some conversations at 60, 120 and 240 fps** (issue #126). When the
  camera looks over Link's shoulder, the game hides him for that shot, but the in-between frames drew
  him again. He now stays hidden on every frame, also in true 60. 30 fps is unchanged.

- **Faster game code: about a quarter less CPU time in the game's own code** (PR #83, thanks
  @depende3000). The recompiler now keeps the game's registers in local variables, stores only the
  condition bits that are read later, and uses cheaper float compares. Measured per game step: −28 % on
  an Apple Silicon Mac, −14.5 % on a Galaxy S25 Ultra. Setup takes about 20 seconds longer to
  generate the game code. With code mods switched on, the game code stays as before for now.

- **Fixed: Cemu graphics packs that write their title IDs without leading zeros were refused** (issue
  #123), e.g. the "Playstation UI" pack (`titleIds = 5000010143600`). The IDs are now compared as
  numbers, the way Cemu reads them.

- **Run/swim speed: a reworked boost** (PR #133, thanks @mhbxyz). Separate multipliers for running
  (1.5x by default) and swimming (1.25x), hold or toggle, and a stamina bar: a boost lasts 5 s by
  default and refills over a 3 s cooldown (set stamina to 0 for the old unlimited boost). A ring under
  the climb wheel shows it. The boost ramps in, and Link's legs keep up with the ground instead of
  skating (true 60 included). Options in Mods > Run/swim speed: use the dash clip instead of walking,
  or a sprint pose with a forward lean and wider arm swing. Boosted runs kick up dust, and a boosted
  surface swim becomes a crawl with splashes and foam, all with the game's own effects. Saved settings
  carry over.

- **Fixed: setup's last screen briefly asked about the shortcut again after Play** (PR #130, thanks
  @rhemfur). Once Play or Quit is pressed, the screen shows "Starting the game..." instead of the
  options; what you had chosen is still done.

- **Windows on ARM: a native `windows-arm64` release** (PR #131, thanks @rhemfur), for Snapdragon X
  laptops and other ARM PCs. 60 fps keeps the game at full speed there by default ("Keep game speed",
  as on Android). The x86-64 release still runs on these PCs too, emulated.

## v0.2.12

- **Android: the app is now part of every release and development build.** Download
  `WindWakerHD-<version>-android-arm64.apk` (x86_64 for emulators and Chromebooks), install it, choose
  your game, and setup builds the game on the phone. The APK contains no game code. It is signed with the
  project's key (certificate SHA-256 `7F:F6:D3:5E:…:B2:13`, full value in the release notes), so later
  versions install as updates.

- **Mods: one-click setup.** Tick a mod (or press **Set up**), confirm once, and its setup runs by
  itself: for example the GameCube sea minimap prepares its island maps and builds itself, then turns
  on. If the game code first needs code-mod support, the game rebuilds and restarts and the setup
  continues afterwards. If a step fails: **Show details** and **Try again**. Changed packages ask for
  confirmation again.

- **Menu and on-screen keyboard: confirm and cancel like in the game** (PR #118, thanks @rhemfur). With
  the default face-button preset (by position) the right button confirms and the bottom one goes back,
  as in the game; the on-screen keyboard types with the game's A and deletes with B. "By label" and
  "Automatic" are followed too.
- **Android player guide** in English, Portuguese and Spanish (PR #122, thanks @rhemfur):
  [docs/android.md](docs/android.md).

- **Diagnostics for menu input problems** (issue #111): `WWHD_OVERLAY_TRACE=1` logs what the settings
  menu receives from the controller and every scroll change; `WWHD_OVERLAY_MOUSE_HANDOFF=0` turns the
  mouse takeover off for such a test. Steps in [docs/overlay-controls.md](docs/overlay-controls.md).

- **Mods tab: installed catalogue mods show where their setup stands.** Each step reads "Done" or
  "To do" (for example the GameCube game step is done once you set it), with a **Go to setup** button
  that jumps to the mod's setup; "Up to date" replaces the greyed-out Update button.

- **Fixed: the game didn't start with a content mod (for example a translation) in the development
  builds** (issue #109). The mod manager switched the replacement files on twice at startup and stopped
  with "Content overrides already activated". Content mods and code mods now start together as they should.

- **Bug reports: the log now lists every connected controller** (name, vendor and product ID, type),
  on connect and disconnect. The game combines all controllers Windows/Linux report, so this shows
  when a second device (e.g. Steam Input's virtual controller) is also sending input.

- **Android: set up the game directly on the phone, no PC needed** (PR #98, tested by @rhemfur).
  Choose your game (an extracted folder, a WUA or a WUD/WUX), and the phone translates and compiles the
  game code itself (several minutes; up to 4 compiler workers depending on the phone), with a progress
  display. Setup pauses when the phone gets too warm or the battery is low, and resumes where it
  stopped. Also new: the GamePad picture on a second screen (foldables, external displays).

- **Mods: one setting for the GameCube version of the game.** Settings → Mods → **GameCube game for
  mods**: choose your GameCube Wind Waker disc image or folder once (it shows the recognised region),
  and every mod that needs it uses it. A mod whose copy is missing can't be enabled and offers a
  button to that setting. Changing the copy marks the affected mods for setup again (prepared files
  are kept).

- **Android: on-screen controls** (PR #88, thanks @rhemfur). Phones without a controller can now
  play: the 🎮 button under the view button (top left) shows or hides two sticks, the D-pad,
  A B X Y, L R, ZL ZR and + − (remembered). They act like a controller, so the button mapping and a
  physical controller keep working; with a controller connected they hide until you touch the screen.

- **Controls: an Automatic face-button preset** (PR #102, thanks @mhbxyz). It reads the labels
  printed on the pad that was plugged in first and follows them, so an Xbox or PlayStation pad plays
  by label and a Nintendo pad by position, without touching the setting.

- **Mods tab: a "Reset to default" button for the catalogue address.** It puts back the official
  mod catalogue if the address was changed. Download errors now also show the server's status code
  (for example "HTTP 404"), so a wrong address is easier to tell apart from a connection problem.

- **Setup can now build with code-mod support right away.** A new "Build with code-mod support"
  option (off by default) saves the second rebuild when you want code mods. Updates and repairs keep
  your choice, and Settings → Mods shows it immediately. Details:
  [docs/setup-code-mods.md](docs/setup-code-mods.md).

- **Fixed: the settings menu scrolled back up after the left stick had been used** (issue #111).
  A left stick that rests slightly off-centre kept scrolling the menu, even while you scrolled with
  the mouse. Now the mouse takes over as soon as you move, click, scroll or drag the scroll bar; the
  controller takes over again with its next new input. Controls:
  [docs/overlay-controls.md](docs/overlay-controls.md).

- **Fixed: in development builds every catalogue mod showed "Unavailable for this port version"**.
  Development builds now carry the last release's version plus the commit (for example
  `v0.2.11-devel.1a2b3c4d`), so the catalogue can check compatibility.

- **macOS: the game has its icon again.** Setup now gives the installed app the game's own icon,
  made from your game files (the release can't ship it), and the game shows it in the Dock while it
  runs. Existing installations get it with the next update or repair.
- **macOS: Vulkan is now included in the release.** Pick it in Graphics > Renderer; nothing to
  install. The Vulkan loader and MoltenVK come with the game and are always the ones it uses (never a
  Homebrew copy). Metal stays the default, and if Vulkan can't start, the game falls back to Metal
  and says why. This also makes Cemu graphics packs with GLSL shaders usable on the Mac.


- **5.1 surround sound** (issue #115). Settings → Audio → Speakers: **Surround 5.1** (restart after
  changing it) sends the game's six separate surround channels, as the Wii U does with its Surround
  TV setting. **Test speakers** plays a tone on each speaker in turn and names it, so the wiring is
  easy to check. Stereo stays the default; if the output can't do 5.1, the game stays in stereo and
  says so. Details: [docs/surround-audio.md](docs/surround-audio.md).
- **Fixed: text fields in the settings overlay didn't accept typing or pasting** (for example the
  catalogue address and the mod search; only deleting worked). Typed text, Ctrl+V / Cmd+V and copy
  now work on all platforms; on Android the on-screen keyboard opens for these fields.
- **Cemu graphics packs that conflict can no longer both be switched on** (issue #68). The Mods tab
  shows "Conflicts with …" before you enable a pack; enabling it asks whether to switch (the other
  pack is turned off). Each pack shows what is active now and what changes after a restart, with a
  **Restart now** button. Profiles that had two conflicting packs on are repaired once, with a note.
- **Fixed: with Fast scene changes on, Grandma kept her back to Link when handing over the shield**
  (issue #116, found and fixed by GreenNaugahyde). The mod no longer creates the new scene's
  characters ahead of time; fades and scene changes stay almost as fast.
- **Fixed: crash when entering the Puppet Ganon room** (issue #90, Vulkan). The room uses a colour
  texture for a depth comparison, which the Vulkan renderer didn't support, so it stopped with an error.
  It now handles it. Such internal errors are also no longer lost: they are written to the log and
  the crash log with their message before the game stops, also on Windows. After updating, setup
  compiles the game code once more.
- **The mouse pointer now hides when you don't use the mouse** (issue #109). After 2 seconds without
  mouse movement it disappears from the game window, in full screen and in a window, on Windows,
  Linux and macOS; moving the mouse shows it again. It stays visible while the settings overlay is open.
- **Fixed: the aspect ratio wasn't remembered** (issue #108). The choice in Settings → Graphics →
  Aspect ratio (or the macOS menu) now stays after a restart, like the other graphics options.
  `WWHD_ASPECT` still overrides it.
- **Mod SDK v2: audio streams.** Code mods can now play their own sound: up to four 48 kHz streams
  per mod, mixed into the game's audio and following its volume and mute (and fast forward). The
  dragon example mod uses it for its melody. Mods without audio don't change anything.

- **Fixed: the settings overlay scrolled by itself, and B closed it while assigning a button** (issue
  #111). A drifting controller stick no longer scrolls the menu: the right stick no longer scrolls
  it at all (the D-pad, left stick and mouse wheel do), and the left stick has a dead zone there.
  Assigning B to a control no longer also closes the overlay.
- **Mod SDK v2: more for code mods.** New public declarations (actor profiles, save inventory,
  animation, matrix emitters, flight position and lighting, song handling) and HUD clipping, used by
  the two example mods in the mod repository: a GameCube-style minimap and the Valoo dragon ride.
- **Setup now uses Python 3.14** (bundled on Windows, downloaded on Linux and macOS as before). Mod
  setup steps can now read GameCube disc images in RVZ format, not only ISO.

- **Setup: more reliable compiler download** (issue #113). If the download of the compiler (about
  190 MB on Windows) is interrupted, setup now retries and resumes where it stopped instead of failing.
  If it still can't download it, you can download the file in your browser and put it in the Wind Waker
  HD folder: setup checks it and uses it. On Windows, when the secure connection can't be verified
  ("unable to get local issuer certificate", usually a root certificate Windows hasn't fetched yet),
  setup now downloads with Windows' own curl instead.
- **Fixed: the boat's sail fluttered too fast at 60, 120 and 240 fps** (issue #68). The sail cloth was
  drawn with its exact pose against the in-between camera, which added an extra jump on every
  in-between frame. Its drawing is now interpolated, and so are the other cloth pieces drawn the same
  way: pirate sails and skull flags, fortress flags, buoy flags and their poles, and capes such as
  Phantom Ganon's. 30 fps is unchanged.
- **New Gameplay mod: fast forward cutscenes and dialogue** (off by default; Settings → Gameplay).
  Hold ZR during a cutscene or a conversation and the game runs 2×, 3× or 4× faster (your choice; the
  button can be changed). Nothing is skipped: the game plays every frame, only faster, so the story
  and events end exactly as without it. Questions still wait for your answer, holding the button in
  normal play does nothing, and letting go returns to your frame rate setting. Sound is muted while
  fast-forwarding (it would play sped up); that can be turned off. Idea and clock from
  GreenNaugahyde's Android fork.
- **Performance overlay: GPU, driver and per-thread timing.** Settings → Graphics → Performance
  overlay now also shows the GPU and its driver, the CPU time of the game and render threads and the
  GPU time per frame (each with its average), so you can see at a glance whether the CPU or the GPU
  is the limit. On Android it adds the system's thermal state and the battery temperature next to
  the GPU load and temperatures. **Copy performance report** includes all of it; please paste it into
  performance bug reports. Ideas and the Android battery handling come from GreenNaugahyde's Android
  fork.
- **Fixed: mini-game countdowns ran too fast at 60, 120 and 240 fps** (issue #105). The letter
  sorting on Dragon Roost could not be won at 60 fps or more; its 30-second limit ran out in 15 (at
  120 fps in about 8). The game's shared countdown (also used by the Windfall auction, the boat race and
  the timed challenges) now counts game steps, not displayed frames, and its display stays smooth.
  30 fps is unchanged.
- **The log file is now off by default.** Turn it on in Settings (F1) > Graphics > Bug reports >
  **Write a log file**; from the next start the game writes `captures/wwhd.log` in its data folder
  (the previous run's is kept as `wwhd-previous.log`), with your user paths removed as in crash logs.
  Please turn it on before reproducing a bug and attach the file to the report.
  `WWHD_LOG_FILE=1` turns it on for one run, `WWHD_LOG_FILE=<path>` writes it elsewhere,
  `WWHD_LOG_FILE=0` turns it off.
- **Mod SDK v2, phase 2: HUD drawing for code mods.** PowerPC code mods can now draw on screen
  (text, images, gauges) on both Metal and Vulkan, ship their own original images and content files
  in one package, and read port settings such as the resolution. See `examples/guest-mods/hud-demo`
  and `button-icons`. Code mods stay off unless you turn on **Enable code mods**.
- **Mods panel: Browse catalogue.** Search, install and update mods from a mod catalogue inside the
  game. It only downloads when you press Refresh, checks every package's size and SHA-256, and
  installs packages disabled. Setup steps (choosing your game dump, preparing files locally,
  building a code mod for your game version) run from the panel. The official mod catalogue is not
  public yet; `WWHD_MOD_CATALOGUE` points the panel at another index.

## v0.2.11

- **Linux: one-file AppImage** (issue #55, PR #101, thanks @mhbxyz), next to the zip. Download it,
  make it executable, run it. It keeps the game, its compiled code, saves and settings in your user
  folders (`$XDG_DATA_HOME/wwhd`, usually `~/.local/share/wwhd`); `--data-dir` picks another folder.
  On distributions without libfuse2 (e.g. Ubuntu 24.04+) start it with `--appimage-extract-and-run`.
  The zip stays as it is.

- **Face buttons by label (Xbox layout)** (issue #78, PR #97, thanks @mhbxyz): settings overlay →
  Controls → *Face buttons* (also the macOS Controls window and Input menu). *By position
  (Nintendo)* stays the default; *by label (Xbox)* makes the button named A act as A, so on an Xbox pad
  A accepts and B goes back.

- **Ultrawide and tall screens: menus fixed** (issue #76, building on PR #16 by @arcadematicas): at
  21:9, 32:9, 16:10 and 4:3 the pause menu, inventory grid, cursor, map and the tabs stay centred at 16:9
  and line up again; backgrounds and the sepia filter still fill the picture.

- **Fixed: black letter card in the Rito mail-sorting game** (issues #28, #69, PR #39, thanks
  @Sean13128): the GPU's "0 × anything = 0" multiply is emulated now (as in Cemu). The shader caches
  rebuild once after updating, so expect a brief stutter on the first start.

- **Fixed: Cemu graphics packs**
  - packs that replace pixel shaders (e.g. Contrasty, NoSSAO, RemoveHUD) partly didn't apply on Vulkan
    since v0.2.10 (PR #87, thanks @rhemfur);
  - packs for the **European** version were refused with "Cemu pack does not target WWHD USA"
    (issue #103). A pack is now accepted when its `titleIds` name the version you installed;
    SDCafiine-style folders named after the title ID follow the same rule.

- **Smoother in busy views on Windows and Android:** the Vulkan buffer cache is now on by default
  everywhere. It keeps unchanged vertex, index and uniform data on the GPU instead of copying them
  every frame: on an RX 6700 XT 13–19% less render-thread time and half the uploads (issue #91, thanks
  @darklinkpower), on a Galaxy S25 Ultra about 34 → 47 fps in heavy Outset views (issue #56, thanks
  @rhemfur). If you see broken or flickering geometry, start with `WWHD_VK_BUFFER_CACHE=0` and please
  report it.

- **Less CPU work:**
  - native versions of the game's two hottest audio loops (PR #81, thanks @depende3000): the audio
    thread uses about two thirds less CPU, with bit-identical sound (also on the European version);
  - the scheduler sleeps while nothing is waiting, and GPU context switches copy only the registers
    that changed;
  - the recompiler no longer emits signed overflow for `mulli`/`mullw`/`neg` (PR #82, thanks
    @depende3000), a latent bug a newer compiler could have turned into wrong behaviour.

- **Android:**
  - when the system driver is too old for the renderer, Adreno phones offer **Install GPU driver…**
    right in the error message (PR #92, thanks @rhemfur);
  - on one-screen views the **GamePad screen appears automatically while the game is paused** (map,
    menus, save prompt) and the TV picture comes back when you resume (PR #59, thanks @rhemfur);
  - precise Vulkan synchronization (barriers) is on by default, where tile-based GPUs are expected to
    gain the most (issue #104). On desktop it's opt-in for testing: `WWHD_VK_NARROW_BARRIERS=1`.

- **Save states remember the controller mode** (Pro Controller or GamePad) and restore it on load.

- **"Keep game speed" explained:** with it off, the whole game runs in slow motion whenever the frame
  target isn't reached (e.g. 120/240 fps at 2x). The setting says so now, is marked as recommended, and
  the performance overlay warns when it happens (issue #91).

- **A log file for every run:** `captures/wwhd.log` in the game's data folder (the previous run's is
  kept as `wwhd-previous.log`), with your user paths removed as in crash logs. Please attach it to bug
  reports. `WWHD_LOG_FILE=<path>` writes it elsewhere, `WWHD_LOG_FILE=0` turns it off.

- **Code mods (Mod SDK v2, off by default):** mods written in C for the console's CPU, translated and
  built on your machine when you install them, for the USA and the European game. Turn them on in
  Settings → Mods → *Enable code mods* (this rebuilds the game code once; with it off nothing changes).
  For modders: [docs/mod-sdk-v2.md](docs/mod-sdk-v2.md).

- **Smaller fixes:** `WWHD_SHADOW_FIX` is gone, use `WWHD_SHADOW_SCALE=1` for console-sized shadow maps
  (issue #67: since v0.2.9 both sizes look practically the same); the buffer cache's verify mode no
  longer reads GPU memory (it ran at ~1 fps on some Windows drivers); a crash at exit with SDL 3.4 on
  Linux is fixed.

## v0.2.10

- **Fixed: the cheats in v0.2.9 wrote to the wrong place in the save data** (Give all items, Master
  Sword + Mirror Shield, 20 hearts / double magic / 5000 rupees) and could damage the save. They write
  to the right place again.
- **Fixed: Medli missing on Dragon Roost after using the Master Sword cheat** (issue #85). The cheat
  gave the full-power Master Sword as *owned*, which the game treats as story progress, so Medli (and
  later Makar) stayed away and the story couldn't continue. The cheat now only **equips** the Master
  Sword and Mirror Shield until the next reload. Saves already affected can be repaired with
  `tools/savegame/wwsave.py repair-medli` (it keeps a backup; see the save tools README).
- **Vulkan:** pixel shaders are now linked to their vertex shader when they are translated, so
  drivers that refused some pipelines get correct shaders from the start (PR #54 by @rhemfur, the
  proper fix for #30).

## v0.2.9

- **Play the European version directly** (title 00050000-10143600): setup now also accepts the
  European game on its own, without the USA version, and builds the port from it through an address
  map derived from the two executables (every function and call matched; contributed by **@ElFDA**,
  PR #77, with ideas from GreenNaugahyde's Android fork). German, Italian, British English, French and
  Spanish are its own languages then. Hooks, mods and save states work as on the USA version.
- **Bloom, distance haze and the sun's glare are back:** the game builds its glow from smaller
  copies of the picture, which the port never made; now it does, on Metal and Vulkan, so the picture
  looks like on the Wii U again. The sun's corona and lens flare react to whether the sun is hidden
  (adapted from GreenNaugahyde's Android fork). New **Settings → Graphics → Effects → Bloom
  strength** (0–200%, default 100%). On Macs, colours now go through the display's colour profile, as
  with Vulkan.
- **Android:** phones whose GPU can't read the game's compressed textures (many Mali and PowerVR
  GPUs) now decode them on the GPU instead of showing black or broken textures; on Snapdragon phones
  you can install and select custom Vulkan drivers such as Turnip (**Settings → Graphics**; a driver
  that crashes or hangs in its first seconds falls back to the system driver on the next start). Both
  follow the approach of [GreenNaugahyde's Android fork](https://github.com/GreenNaugahyde/ZeldaWWHDRecompAndroid),
  rebuilt on our renderer. Not yet tested on real Mali/PowerVR/Snapdragon devices: reports welcome.
- **Run and swim faster** (optional, Mods tab, off by default): hold L3 (or toggle) to boost Link's
  running and swimming speed, 1.25x to 4x; works with true 60. The idea comes from GreenNaugahyde's
  Android fork.
- **Average frame rate** in the performance overlay (and GPU load and temperatures on Android where
  readable); **crash logs** now include the settings in use, with your user paths removed, and Android
  offers to share the log after a crash.
- **Vulkan:** the compressed-texture feature is now enabled as the Vulkan specification requires.
- **Screenshot key:** **F10** saves the TV picture as a PNG in a `screenshots` folder next to the save
  states (`~/Library/Application Support/wwhd/screenshots`, `%APPDATA%\WWHD\screenshots`,
  `~/.config/wwhd/screenshots`; `data/user/screenshots` in a release folder), named
  `WindWakerHD_YYYY-MM-DD_HH-MM-SS.png`. It is the frame shown when you press the key (also at 60/120/240
  fps), at the internal resolution and aspect ratio (2x at 21:9: 3414x1440), with the game's own effects
  and FXAA when on, but without the settings overlay or notices; "Screenshot saved" shows briefly. The
  key can be changed (or a controller button added) in Controls ("Photo"); the Saves tab has **Open
  screenshots folder** and an option to also save the GamePad screen (`..._GamePad.png`) while it is
  shown. Saving happens in the background: the game does not stutter. Both renderers, all platforms.

- **Frame interpolation fixes (60/120/240 fps and true 60):** the waving flags on Dragon Roost no
  longer turn into huge stretched polygons on the in-between frames (issues #36, #70); the boat's sail
  and yard and items Link carries (a bomb) no longer jump for single frames (issue #68); the stars in
  the night sky no longer wobble when the camera turns (issue #68). 30 fps is unchanged.
- **Fixed: the pause menu would not close at 120/240 fps** (issues #64, #74; probably also #73, no
  control after an item-get message). The HD screens (the TV pause screen and others) advanced on every
  drawn frame instead of once per game step, so the TV pause screen reopened itself right after
  closing. They now update once per step, which also gives their fades their 30 fps speed back at
  60/120/240 fps.

## v0.2.8

- **Fixed: the game could freeze at startup with 0 fps on some Macs** (issue #62, MacBook Air M1).
  Newer Apple compilers turned the game's spin-wait loops into endless loops that never saw the other
  core release the lock. Every loop in the game code now re-reads memory on each pass; no measurable
  speed cost.
- **Fixed: Android crashed when changing the aspect ratio or internal resolution on some phones**
  (issue #72, Adreno 830). Phones whose driver cannot blit depth buffers now copy them with a small
  draw instead; nothing aborts any more if a device can do neither. Thanks to @Blivii for the analysis
  and the patch.
- **Fixed: grid pattern in contact shadows at higher internal resolutions** (issue #66). The game's
  shadow and ambient-occlusion blur now covers the same area as on the console at every resolution;
  1x is unchanged and there is no measurable speed cost. Shadow maps still scale with the internal
  resolution for sharp shadows; `WWHD_SHADOW_FIX=1` keeps them at the console's 1024x1024 instead,
  for soft edges that never shimmer (issue #67).
- **More languages (experimental):** with your own dump of the European or Japanese game, the port can
  use its text, fonts and menus: German, Italian, British English, European French and Spanish, or
  Japanese. See [docs/language-packs.md](docs/language-packs.md).
- **Fan translations as content mods**, including **Arabic and Hebrew** drawn right to left (issue #60),
  see [docs/mod-manager.md](docs/mod-manager.md) and [docs/rtl-text.md](docs/rtl-text.md).
- **macOS: closing the TV window quits the game** (issue #65), as on Windows and Linux. During a game
  it asks first: **Quit**, **Cancel** or **Save State and Quit**. `WWHD_QUIT_PROMPT=0` turns the
  question off.
- **Faster on Linux and Steam Deck (Vulkan):** the guest buffer cache is now on by default on desktop
  Linux, as on macOS. It keeps unchanged vertex, index and uniform data on the GPU instead of copying it
  every frame, which removed a 20 fps lock in busy views on an RK3588 board (issue #50). If you see broken
  or flickering geometry, start with `WWHD_VK_BUFFER_CACHE=0` and please report it. Windows and Android
  stay opt-in (`WWHD_VK_BUFFER_CACHE=1`).
- **Save states are now small and can go into bug reports.** The **Save state** button (and
  Shift+F1–F5, the Save States menu) now writes a *portable* state, `slotN.wwstate`: a few KB with
  your progress (the same save data the game writes into `cking.sav`) and where Link stands (stage,
  room, position, facing, time of day). It contains no game code and no game data, so you can attach
  it to an issue; **Copy save for bug report** in the Saves tab copies the paths of the state and of
  your `cking.sav`. Loading one puts that progress into the running game and takes Link there; it is
  not an exact snapshot (enemies, a running cutscene and other actors start fresh). The old full
  states (the whole running game, ~300 MB, contain game data: never share them) are still there for
  debugging: Saves › *Full save states*, or `WWHD_FULL_SAVE_STATES=1`. Loading a slot loads either
  kind; an older full state in a slot is kept and the Saves tab says so. A portable state can only be
  saved while you control Link (not during a cutscene or dialogue); one saved on the boat puts Link
  back on the boat. See [docs/portable-save-states.md](docs/portable-save-states.md).
- **Fixed: "Quick doors" could crash the game going through a door** (issue #61, "PROGRAM HALT
  J3DPacket.cpp:157"; reported in Tingle's jail on Windfall). The extra game steps that make doors
  quicker also deleted actors (a pot, a rat, Tingle) faster than the game allows: an actor that
  removed itself while a door opened was freed before its last drawn frame was done with. Deleting
  now keeps its normal pace; doors are as quick as before.
- **Gyro aiming fixes** (issues #45 and #71): a new **Turn left/right by** setting (settings overlay →
  **Controls** → **Gyro…**) with the usual gyro conventions: **Player space** (default: turn the controller
  left or right about the real vertical, however you hold it), **Yaw** (its own vertical axis) or **Roll**
  (tilt it like a steering wheel). Before, rolling the controller turned the view and turning it depended
  on how you held it. The default sensitivity is lower (0.5x, about one to one with your controller; the
  range now goes down to 0.05x), and settings that still have the old default start at the new one.
  Gyro aiming now also works in **Pro Controller** mode. For reports that the gyro stops after a while:
  the port no longer stops when a controller's sensor timestamps stall, switches to the controller you
  move when several are connected, turns a controller's sensors on again when they fall silent, and logs
  `[gyro]` lines that say why motion stopped (including a drifting right stick, which makes the game
  ignore the gyro). "Recenter" is now **Recalibrate** (learns the gyro's offset anew).

## v0.2.7

- **Fixed: taking a picture with the Picto Box crashed the game (Vulkan)** (issue #53). Right after
  the shot the game renders small 3D colour-grading textures slice by slice, which the Vulkan renderer
  did not support; on Metal the preview was black. Both renderers now render into 3D textures, and the
  saved pictures show up in colour in the Picto Box album (they were black on both renderers before).
- **Fixed: black shadows on macOS (Metal)** (issue #47). When macOS had to compile the game's shaders
  from scratch (first start, or after a macOS update cleared its shader cache), the ambient-occlusion
  pass could be skipped at the title screen while its shader was still compiling; the light buffer was
  then created with the wrong layout and stayed wrong for the whole session, turning shadowed areas
  black until a restart. Render targets no longer depend on that timing, and draws whose result the
  game reuses are never skipped. A cold start now spends about 0.7 s more on the title screen once.
- **Windows setup without PowerShell** (issue #58): the setup no longer runs a PowerShell script or
  removes the "downloaded from the internet" mark, and `Wind Waker HD.exe` downloads nothing: the
  official, signed embeddable Python now ships in the release (the Windows zip grows to about 19 MB).
  The programs carry version information and a manifest. Some antivirus engines (BitDefender and
  engines using it) may still flag the unsigned exe; that is a false positive and has been reported.
- **120 and 240 fps** (settings overlay → **Graphics** → **Frame rate**, or the macOS Graphics
  menu): frame interpolation now also draws 3 or 7 blended frames between the game's 30 logic steps
  a second, for 120 Hz and 240 Hz displays. Camera, models, particles, sea and every other blended
  effect move at even steps between two game steps; input, sound and menus behave as at 30 fps.
  The game detects the display's refresh rate (ProMotion Macs: 120 Hz; Windows, Linux and Android:
  the monitor's or phone's current rate, and Android phones are asked for their fast mode) and the
  overlay shows it ("Your display: 120 Hz"). A rate the display cannot show is capped to it:
  240 fps on a 120 Hz display draws 120, and 120 fps on a 60 Hz display draws 60. **Keep game
  speed** is on by default at 120/240 fps: when the computer cannot draw every frame, the game
  still runs at full speed with fewer in-between frames. Measured on
  an M3 Max with its 120 Hz display (Outset Island): 120 fps draws 115 frames a second on screen
  with Vulkan and 108 with Metal, at 29.1–29.7 game steps a second; without presenting, 119.3
  frames and 29.9 steps. 240 fps is limited by the renderer on that machine (about 146 frames a
  second when not capped to the display), so on a 120 Hz screen it draws the same as 120 fps.
- **"Uncapped" debug switch** (settings overlay → **Graphics**; not saved): no frame limit and no
  vsync, to see how many frames a second your computer can draw (window title and performance
  overlay). The game counts frames, so it runs faster than normal while it is on: not for playing.
  Metal and Vulkan; `WWHD_UNCAPPED=1` turns it on at start.
- **Keep game speed recovers faster after a hitch** (all frame rates): one slow frame (a shader
  compile, a scene load) no longer turns the in-between frames off for several seconds.
- **Android:** full-size occlusion depth is off by default (it halved the worst GPU waits on an
  Adreno 830, issue #56); the settings overlay can still turn it on.
- **Smaller fixes:** cheaper reuse checks of upload memory on Macs and integrated GPUs (follow-up to
  the v0.2.5 fix for issue #44), time limits for the Android CI build, and "mouse as
  gyro" no longer loses part of a movement when a frame stalls.

## v0.2.6

- **Far fewer shader translations and pipelines, fewer stutters in new areas, less memory
  (Vulkan):** the renderer now keys a translated shader only on the state that actually changes its
  translation (for example, only the textures a shader samples and the vertex inputs and outputs it
  uses), so the same shader is no longer translated again and again for different render states.
  Starting from an empty shader cache, Outset Island needed 569 translations and 204 pipelines
  instead of about 6,100–6,700 and 3,200–3,500, and Windfall 829 and 236 instead of about 4,300–5,000
  and 2,300–2,700. That takes about 100–115 MB less memory, cuts the frames that take over 50 ms
  by about a third, and lowers render-thread CPU time by 4–12%. It supersedes PR #46 by rhemfur,
  whose idea (keys that translate to the same shader share it and its pipelines) is part of it.
- **Performance reports say which build and system they come from** (issue #44): **Copy performance
  report** now starts with the version and commit, the operating system and version, the graphics card
  with its driver and Vulkan version (or the Metal device), and the rendering switches that are not at
  their defaults (buffer cache, CPU paths turned off, lazy DrawDone / async present turned off, the
  gyro source). The log's first lines name the version and system too, so crash logs carry them.

## v0.2.5

- **Fixed: much lower frame rate on Windows and Linux PCs with a dedicated graphics card (Vulkan)**
  (issue #44). Three renderer shortcuts that v0.2.4 turned on for all platforms compared new vertex
  and uniform data against the previous copy in the GPU's upload memory; reading that memory back is
  very slow on AMD and NVIDIA cards (one report went from 30 fps in v0.2.1 to 12 fps). The renderer
  now keeps those comparison copies in normal memory and never reads GPU upload memory, which also
  speeds up an older index-data path when the buffer cache is off.
- **Gyro aiming** (issue #45; settings overlay → **Controls** → **Gyro…**): aim the bow, hookshot,
  boomerang, telescope, Picto Box and grappling hook in first person by moving your controller, as
  with the Wii U GamePad. Sources: the gyro of a **DualSense, DualShock 4, Switch Pro, Joy-Con or
  Steam Deck** controller, a **Cemuhook (DSU)** server (DS4Windows, BetterJoy, phone apps;
  127.0.0.1:26760 by default), or the **mouse** (for Steam Input's "gyro to mouse"). Sensitivity,
  invert and a recenter button are adjustable. Off by default; the game's own **Options → Gyro**
  switch still applies. See `docs/gyro.md`.

## v0.2.4

- **Smoother 60 fps on slower PCs (Vulkan):** the render thread no longer waits for the GPU after
  every frame on Windows, Linux and macOS (the way Android already worked), so CPU and GPU work in
  parallel. Where the render thread is the limit, this is the difference between slow motion and
  full speed: in our load tests the game went from 35–40 fps with only a third to half of the 60 fps
  in-between frames drawn to a steady ~59 fps with almost all of them, at full game speed. Without
  a frame limit it renders 13–53% more frames per second, depending on the scene. (Issues #7, #44)
- **Less CPU work per frame (Vulkan):** 15 renderer shortcuts that were Android-only are now on
  everywhere (8–14% less render-thread time), and on macOS vertex, index and uniform data the game
  does not change stay on the GPU instead of being copied again for every draw (another 6–17% less
  render-thread time and 43–64% less data uploaded per frame). Windows, Linux and Android players
  can try that cache with `WWHD_VK_BUFFER_CACHE=1`; `docs/vulkan.md` ("Guest buffer cache") says
  what to report.
- **Performance report** (settings overlay → **Graphics** → **Copy performance report**): copies a
  breakdown of the render thread's time per frame to the clipboard, for bug reports about speed.
- **macOS: setup works when the app is opened straight from the downloaded folder** (issue #48).
  Every setup error now says what failed, what to do, and where the log is.
- **Sound in GamePad-only mode** (Off-TV Play, the Minus button): the game's sound now plays through
  your speakers instead of going silent (issue #49).

## v0.2.3

- **Mod manager** (settings overlay → **Mods**): the built-in mods (direct and mouse camera,
  first-person shortcut, wall climbing, quick doors, fast scenes) in one searchable list, plus
  **installable mod packages** from a folder or a `.wwhdmod` ZIP, with profiles, dependencies and
  per-mod options. Everything starts off; nothing from a package loads until you enable it.
  - **Content mods** replace game files without touching your game folder.
  - **Cemu graphics packs** (`rules.txt`) can be imported, with their presets and resolution rules;
    shader packs need the Vulkan renderer. Code patches from Cemu packs are not supported.
  - **Native mods** (packages with their own compiled code) ask for a one-time confirmation before
    they are enabled, because they run with the game's full permissions; only enable mods from
    sources you trust.
  See `docs/mod-manager.md` for the package format and the mod SDK.

## v0.2.2

- **Controller rumble fixed** (issue #35): rumble now follows the game's patterns exactly and always
  stops: on quit, after a crash, when the game hangs, while the settings menu is open and when the
  window is in the background. Before, a controller could keep vibrating until it was switched off.
  New **Rumble** on/off option in the settings overlay (Controls tab).
- **Older graphics drivers** (issue #37): Windows/Linux builds no longer refuse to start with
  "Entry Point Not Found" on drivers without Vulkan 1.3; they use the `VK_KHR_dynamic_rendering`
  extension where the driver offers it, and otherwise show a clear message naming the GPU, its
  driver version and what is missing.
- **Language tab:** only the languages your game contains can be chosen (the USA version has
  English, French and Spanish), and a changed language says that it applies after a restart.
- **Textures update exactly:** textures the game changes in memory are now always re-uploaded (on
  Metal and Vulkan); before, a change could show up a few frames late.
- **Full screen is remembered on Windows and Linux too** (issue #43), as it always was on macOS:
  leave the game in full screen and it starts in full screen next time.
- **Better crash logs:** a crash outside the game code names the library it happened in (for
  example the graphics driver), and the log lists the Vulkan layers that overlays add, so crash
  reports can be answered much faster.

## v0.2.1

- **Cemu archives (`.wua`)** (issue #27): the setup now also takes a Cemu `.wua` file; it is
  already decrypted, so no keys are needed. Every source (disc image, `.wua`, extracted folder) is
  checked for the right game version first (USA, version 0), with a clear message if an update is
  merged in or the region is different.
- **Linux on arm64** (aarch64): a separate `linux-aarch64` download (Raspberry Pi 5, Asahi Linux, ARM
  laptops).
- **On-screen text entry** (issue #29): the name screen now shows a text window over the game, with an
  on-screen keyboard for controllers and the mouse; typing on the keyboard goes straight into it.
- **Boot crash fixed:** the rare crash right after start ("Prepare Thread", agl shader setup) is gone.
  GX2CopySurface now completes before it returns, as the game expects; before, a late render thread
  could write into memory the game had already reused (about every 15th start under load, every start
  on some phones).
- **GamePad / Pro Controller choice is saved** between launches (issue #26).
- **Cheats** now also work on saves with heart pieces (PR #25 by Sean13128).
- **Android** (rhemfur, PRs #30, #31): pipelines the Adreno driver refuses no longer close the game,
  the game threads use at least two fast cores, and arm64 builds treat `char` as signed, as on the
  console.

## v0.2.0

- **Portable releases.** Unzip anywhere and start **Wind Waker HD**: the first start prepares
  the game once from your own dump (releases never contain game code, so it is built on your computer);
  later starts launch the game directly. Everything — the built game, the game files, saves,
  settings, save states, shader caches, logs and the downloaded compiler — stays in the release
  folder; nothing goes to your user folders unless you ask for a shortcut. An extracted game folder is
  used where it is instead of being copied. Saves and settings can be copied over from an earlier
  installation. Hold Shift while starting (or `--setup`) to repair, update or change the game.
- **Settings overlay** (Dear ImGui): an in-game menu for everything in one place — save states and
  Crash Recovery, graphics (renderer, frame rate, resolution, aspect ratio, AO, filtering, FXAA,
  performance overlay), display, gameplay mods and cheats (Graphics also has the Vulkan presentation mode), controls and language. Open it with
  **F1** (Fn+F1 on most Mac keyboards), **Cmd+,** / *Settings…* on macOS, or hold **Select** / press
  **Home** on a controller; it works with mouse, keyboard and controller on every platform and
  renderer. The Controls tab shows the controller drawing with live feedback of pressed buttons.
  Shift+F1 still saves state slot 1; slot 1 now loads from the overlay.
- **GamePad screen modes on Windows/Linux** (overlay → Display): separate window, picture-in-picture,
  automatic overlay, off, or GamePad only, as on macOS; clicks on the GamePad picture reach the game.
- **Full screen is remembered on Windows/Linux too** (issue #43): the TV window starts as it was left,
  in full screen or in a window, as the macOS app always did. `WWHD_FULLSCREEN=0|1` overrides it for
  one start.
- **60 fps "Keep game speed"** (overlay → Graphics → Frame rate, PR #21 by rhemfur): skips in-between
  frames instead of slowing the game down when the machine can't draw 60 frames a second; off by
  default. The performance overlay shows the share of in-between frames drawn.
- **Android: build it yourself** (rhemfur's port): see
  [Android (build it yourself)](README.md#android-build-it-yourself). Also by rhemfur (PRs #19–#22, #24): the game's own icon for windows and shortcuts, name typing in every game window and optional
  Vulkan paths for slow devices.
- **Performance pass** (PR #15 by Sean13128): much less render-thread CPU on Metal (no more stutter
  while the shader cache warms up), a lighter vsync wait on Vulkan, and a fix for the both-renderer
  build crashing with Homebrew boost installed.
- **Cheats** (Gameplay menu / overlay, PR #15): all items, best sword and shield, 20 hearts, double
  magic, 5000 rupees, infinite health/magic/ammo, and story cheats (songs, Triforce shards, dungeon
  items, keys) — use a spare save file for those.
- **Graphics options are remembered** between launches (macOS since PR #15; Linux/Windows in
  `settings.ini`).
- **Controller rumble** (PR #13 by arcadematicas).
- **Linux/Windows**: GamePad touch with the mouse in the GamePad window, F11 / Alt+Enter full
  screen, closing the TV window quits (closing the GamePad window hides it), the name-entry text
  prompt works again (PR #14 by rhemfur), 1 ms timer resolution on Windows for smoother frame pacing
  (PR #12 by rhemfur), and a `--unwindlib=libgcc` build note for clang setups with libunwind.
- **Console language**: `WWHD_LANGUAGE=<code>` (or the overlay's Language tab) picks the game's
  language from those on the disc.
- **Native Windows LLVM builds** (PR #17 by resadent): build with clang and Visual Studio's Windows
  SDK, without MSYS2 (missing dependencies are built from pinned sources); smoother Vulkan frame
  pacing on Windows via SDL's high-resolution sleeps.
- **Vulkan presentation mode** (overlay → Graphics): *Vsync* (FIFO, default), *Low latency*
  (MAILBOX, where the driver offers it) or *Off* (IMMEDIATE); switches live and is remembered.
  `WWHD_VK_PRESENT_MODE` overrides it.
- **Faster Vulkan on Windows/Linux** (PR #18 by resadent): bounded draw batching and a higher
  game/render thread priority are now on by default, as on macOS.
- **GameCube save converter** (`tools/savegame`): bring your GameCube save file into HD — see
  [Optional: bring your GameCube save to HD](README.md#optional-bring-your-gamecube-save-to-hd).

## Before v0.2.0

- **Linux and Windows builds** (Vulkan renderer with an SDL3 host), with automatic CI builds for both.
  Fixes from the first Linux reports: game paths are resolved case-insensitively (the game asks for
  `Audiores`, the disc folder is `AudioRes`; this crashed the game right after startup), build fixes
  for newer compilers, `WWHD_NO_GAMEPAD` only hides the GamePad window (`WWHD_NO_CONTROLLERS` turns
  off controllers), and a hint where to type when the game asks for text.
- **Crash logs and Crash Recovery**: every crash writes `captures/crash-<time>.log`. Crash Recovery
  (Save States menu, off by default) keeps automatic save states plus the recorded input, so a crash
  can be reproduced with `WWHD_REPLAY=<n>`.
- **`wudextract.py`**: the disc key file can be 16 raw bytes or 32 hex digits, with clear errors for
  a missing or non-matching key.
- **True 60 (key 7, experimental)**: every 30 Hz step is now exactly the 30 fps game's step (game
  logic, saves and quests stay as in the original); hookshot crash fixed. For smooth 60 fps,
  interpolation (key 6) is the recommended mode.

- **Vulkan renderer** (by OpenAI Codex), built into the same app next to Metal. Pick one in
  **Graphics › Renderer**; the choice is saved and used from the next start ("Restart Now"
  relaunches right away). Both share the same windows, menus, display modes, controls and mods.
  If Vulkan can't start (no Vulkan loader or MoltenVK installed), the game falls back to Metal and
  says why. Details: [docs/vulkan.md](docs/vulkan.md).
- **Full screen and GamePad screen modes** (Display menu): full screen for the TV window (⌘F),
  picture scaling (smooth, sharp, integer), and the GamePad screen as its own window, a
  picture-in-picture overlay, an automatic overlay that pops up when the GamePad picture changes,
  or off (⌘G shows/hides it).
- **Aspect ratio** (Graphics › Aspect ratio): 16:9 (original), match the window, 16:10, 21:9 or
  32:9. Wider screens see more to the sides (same vertical view); the HUD stays at the edges and
  menus stay centred.
- **Fixes**: misplaced Yes/No cursor in text boxes at 16:10, quitting with ⌘Q could hang, garbled
  characters in the window title. Community fixes from pull requests #1 and #2 (Miiverse manager
  throttling, shared shader-cache memory) are included.

- **60, 120 and 240 fps.** Modes in the Graphics menu and the settings overlay (Graphics › Frame rate):
  - **60 fps (key 6), 120 fps, 240 fps**: frame interpolation. The game logic keeps its original
    30 steps per second; the frames in between (1, 3 or 7 per step) are drawn blended between two
    steps (camera, models, particles, sea, wave crests, grass and trees, cloth, weather, lighting).
    Input, sound and menus behave as at 30 fps. The frames reach the screen up to the display's
    refresh rate (the overlay shows it, e.g. "Your display: 120 Hz"). "Keep game speed" (on by
    default at 120/240 fps, off at 60) skips in-between frames the computer or display cannot show
    instead of slowing the game down.
  - **True 60 (key 7, experimental)**: Link and the follow camera run their logic at 60 steps per
    second (for the actions that have been converted and measured against the original); everything
    else runs at 30 and is interpolated.
- **Higher internal resolution** (1x / 1.5x / 2x / 3x, key R) and **edge smoothing** (FXAA, key 8).
- **Save states**: a Save States menu with 5 slots (Shift+F1–F5 save, F1–F5 load), kept across
  sessions in `~/Library/Application Support/wwhd/states/`.
- **Controls window** (Input › Controls…): a drawing of the Wii U GamePad or Pro Controller; click
  a button to remap it to a key or a controller input, live feedback of pressed buttons and stick
  positions, conflict warnings.
- **Optional gameplay mods** (Gameplay menu, all off by default): climb any wall, direct right-stick
  camera, mouse camera, first person on the mouse wheel, quick doors, fast scene changes.
- **Fixes**: shadow streaks, flicker after loading, doubled wave sounds at 60 fps, camera issues.
- **Tools**: function naming against the GameCube decompilation (`tools/decomp/`), a differential
  harness that verifies hand-written source against the recompiled original (`tools/verify/`), and
  the 60 fps conversion tools (`tools/true60/`). See "Optional: decompilation tools" below.
