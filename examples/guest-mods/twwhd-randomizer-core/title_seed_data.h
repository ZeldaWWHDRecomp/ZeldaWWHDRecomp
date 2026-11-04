/* MIT tweaks.cpp::modify_title_screen. Initialize before native static
 * constructors: 026EBA20 copies 10108280 into the English title position.
 * Native getters 026EBD88/026EBD94 read the two scale globals directly. */
#pragma once
WWHD_HOOK(0x028EA120, randomizer_initialize_title_data, (void)) {
    WWHD_GAME_DATA(0x101F7048, f32) = 1.4f;
    WWHD_GAME_DATA(0x101F7044, f32) = 2.25f;
    WWHD_GAME_DATA(0x10108280, f32) = -38.0f;
}
