/* Complete MIT upstream remove_music.asm semantics. Per-seed compilation
 * registers these six early-return consumers only when the validated option is
 * enabled. OFF emits no descriptors, leaving all native functions untouched. */
#pragma once
#ifndef WWHD_RANDOMIZER_REMOVE_MUSIC
#error "Regenerate prepared seed data: missing validated music policy"
#elif WWHD_RANDOMIZER_REMOVE_MUSIC != 0 && WWHD_RANDOMIZER_REMOVE_MUSIC != 1
#error "Invalid validated music policy"
#elif WWHD_RANDOMIZER_REMOVE_MUSIC
WWHD_REPLACE(0x0202204C,void,randomizer_music_bgm_start,(u32 self,u32 id,u32 fade,u32 flag)){
    (void)self;(void)id;(void)fade;(void)flag;
}
WWHD_REPLACE(0x0202879C,void,randomizer_music_battle,(u32 self,f32 distance)){
    (void)self;(void)distance;
}
WWHD_REPLACE(0x0201DE00,void,randomizer_music_sub_bgm,(u32 self,u32 id)){
    (void)self;(void)id;
}
WWHD_REPLACE(0x02027F04,void,randomizer_music_stream,(u32 self)){
    (void)self;
}
WWHD_REPLACE(0x02897F48,u32,randomizer_music_prepare_label,(u32 self,u32 handle,u32 label,u32 info)){
    (void)self;(void)handle;(void)label;(void)info;return 2;
}
WWHD_REPLACE(0x02030F1C,u32,randomizer_music_stream_ready,(u32 index)){
    (void)index;return 1;
}
#endif
