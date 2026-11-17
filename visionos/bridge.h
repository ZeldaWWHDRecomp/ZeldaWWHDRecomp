// Apple Vision Pro: the functions the game (gfx/host_tvos.mm, gfx/vision_screen.mm) and its SwiftUI parts
// (visionos/*.swift) call in each other. Both sides include this header, so the compiler checks them.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <CoreAudioTypes/CoreAudioTypes.h>
#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// the TV picture as the 3D screen gets it, per eye (16:9)
#define WWHD_SCREEN_WIDTH 1920
#define WWHD_SCREEN_HEIGHT 1080

void wwhd_vision_log(const char* _Nonnull text);
void wwhd_vision_event(const char* _Nonnull what);  // (diagnostic: captures/vision-events.log with WWHD_VISION_EVENTS=1)

// ---- the game, called from Swift
// the game window's 3D screen shows (ON) or is gone (gfx/vision_screen.mm)
void wwhd_screen_attach(int on);
#ifdef __OBJC__
// the newest pictures, when newer than *GENERATION: the left and right eyes' (the same for a mono frame); their *SLOT
// is held until wwhd_screen_release(SLOT) (the screen copies them in a command buffer and releases them when it
// completes)
bool wwhd_screen_acquire(uint64_t* _Nonnull generation, int* _Nonnull slot,
                         id<MTLTexture> _Nullable __autoreleasing* _Nonnull left,
                         id<MTLTexture> _Nullable __autoreleasing* _Nonnull right);
#endif
void wwhd_screen_release(int slot);
// the game's 5.1 mix played by the game window's entity (false: no 5.1 playing), and its audio (gfx/host_tvos.mm)
bool wwhd_audio_entity(int on);
// RealityKit's audio thread: FRAMES of one channel of the mix (0 L, 1 R, 2 C, 4 Ls, 5 Rs; the LFE is in L and R)
void wwhd_audio_render_channel(int channel, AudioBufferList* _Nonnull list, uint32_t frames);
// the GamePad window's view (retained), and whether it shows (gfx/host_tvos.mm)
void* _Nonnull wwhd_gamepad_view(void);
void wwhd_gamepad_shown(void* _Nonnull view, int on);
// the game's window went away (WHY: for the log): the game ends if its session is gone too (gfx/host_tvos.mm)
void wwhd_game_window_gone(const char* _Nonnull why);
// whether the game's window is up (the app may start with only the GamePad's window restored)
bool wwhd_game_window_up(void);
// the game window's view controller (retained): the game's view under the 3D screen; and once it is in its window
void* _Nonnull wwhd_game_view_controller(void);
void wwhd_game_window_shown(void* _Nonnull controller);

// ---- Swift, called from the game
// the game window's 3D screen: a retained UIHostingController to lay over the game's view (StereoScreen.swift)
void* _Nonnull wwhd_screen_view(void);
// opens the GamePad's window, or brings it back in front (GamePad.swift)
void wwhd_gamepad_open(void);
// closes the GamePad's window (GamePad.swift)
void wwhd_gamepad_close(void);

#ifdef __cplusplus
}
#endif
