/* Control-flow adapter from public CC0 wwhd_src/d/d_s_open.cpp. Experimental source port. */
#include "wwhd/save_flags.h"
#include "newgame_init.h"
#include "classic_demo_end.h"
#include "classic_fall_exit.h"
#include "classic_abe.h"
#include "classic_pause.h"
#include "classic_tact_wait.h"
#include "remove_music.h"
#include "particle_performance.h"

#include "quiet_sail.h"
#include "warp_song_npcs.h"
#include "warp_song_ship.h"
#include "warp_song_link.h"
#include "casual_clothes.h"
#include "casual_reflection.h"
#include "recollection_inventory.h"
#include "ship_sail_dialogue.h"
#include "korl_hint_messages.h"
#include "ship_boundaries.h"
#include "ship_boarding.h"
#include "reverse_entrances.h"
#include "komali_room.h"
#include "drc_lava.h"
#include "time_passage.h"
#include "grandma_music.h"
#include "dpad_shortcuts.h"
#include "stage_unload_bgm.h"
#include "outset_music_frame.h"
#include "item_presentation.h"
#include "item_get_presentation.h"
#include "shop_item_presentation.h"
#include "item_resources.h"
#include "field_item_pickup.h"
#include "field_item_access.h"
#include "custom_item_drops.h"
#include "reward_data.h"
#include "fixed_seed_data.h"
#include "permanent_resource_heap.h"
#include "title_seed_data.h"
#include "windfall_seed_data.h"
#include "fast_chests.h"
#include "hookshot_sight.h"
#include "low_health_effects.h"
#include "item_get_music.h"
#include "beedle_blue_jelly.h"
#include "cannon_boomerang.h"
#include "seed_boss_data.h"
#include "boss_rewards.h"
#include "reward_tingle.h"
#include "reward_flight.h"
#include "reward_orca.h"
#include "reward_great_fairy.h"
#include "magic_upgrade.h"
#include "save_restore_caps.h"
#include "jabun_wall.h"
#include "forbidden_woods_flower.h"
#include "swordless_pig.h"
#include "fire_mountain_firewall.h"
#include "pearl_progression.h"
#include "story_shortcuts.h"
#include "swordless_combat.h"
#include "swordless_lifecycle.h"
#include "shieldless_hammer.h"
#include "hurricane_spin.h"
#include "leaf_fan.h"
#include "salvage_chart_gate.h"
#include "reward_maggie.h"
#include "reward_marie.h"
#include "reward_lenzo.h"
#include "reward_ivan.h"
#include "reward_battlesquid.h"
#include "reward_zunari.h"
#include "people_sail_dialogue.h"
#include "fishman_sail_dialogue.h"
#include "reward_salvage_corp.h"
#include "reward_cafe_postman.h"

#include "reward_doc_bandam.h"
#include "reward_present.h"
#include "reward_tree.h"
#include "reward_beedle.h"
#include "beedle_stock.h"
#include "quest_reward_flags.h"
#include "npc_availability.h"
#include "sage_dungeon_reentry.h"
#include "kogoli_availability.h"
#include "windfall_pedestals.h"
#include "jabun_whirlpool.h"
#include "lenzo_gallery_order.h"
#include "zephos_single_reward.h"
#include "drc_wind.h"
#include "drc_quake.h"
#include "drc_lod.h"
#include "drc_cloud.h"
#include "drc_heat.h"
#include "last_ocean_savewarp.h"
#include "korok_availability.h"
#include "switch_actors.h"
WWHD_GAME_ORIGINAL(0x025AEC74, void, original_change_scene, (u32 scene));
static int opening_active,filter_installed;
static u32 pulse;
static void skip_opening_input(wwhd_input_state* input) {
    if (!opening_active) return;
    if (++pulse & 1) input->buttons |= 8;
    else input->buttons &= ~8u;
}
WWHD_HOOK(0x025AED3C, enter_opening_execute, (u32 scene)) {
    opening_active = *(volatile s16*)(scene+8) == 12;
    if (opening_active && !filter_installed) {
        filter_installed=wwhd_input_filter_register(skip_opening_input);
        wwhd_log_int("coremod opening input filter registered",filter_installed);
    }
}
WWHD_HOOK(0x025AEF3C, leave_opening_scene, (u32 scene)) {
    (void)scene;opening_active=0;
}

WWHD_REPLACE(0x025AEC74, void, opening_change_scene, (u32 scene)) {
    if (*(volatile s16*)(scene+8) != 12) { original_change_scene(scene);return; }
    if (wwhd_fopOvlpM_IsPeek()) return;
    if (!wwhd_fopScnM_ChangeReq(scene,7,0,5,1)) return;
    opening_active=0;
    initialize_randomizer_save();
    *(volatile u32*)(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x116C)=0;
    const u32 name=wwhd_dComIfG_getGameInfo()+0x5140;
    const s32 room=*(volatile s8*)(wwhd_dComIfG_getGameInfo()+0x514A);
    const s32 layer=*(volatile s8*)(wwhd_dComIfG_getGameInfo()+0x514B);
    wwhd_setSceneName_025E17CC(name,room,layer);
    wwhd_log("coremod opening seam complete");
}

WWHD_HOOK_RETURN(0x025B5060, initialize_starting_location, (u32 place)) {
    *(volatile u8*)(place+8)=(u8)init_start_room;
    *(volatile u8*)(place+9)=(u8)init_start_spawn;
}

#include "temple_tablets.h"
#include "tower_servant_events.h"
#include "tower_servant_execute.h"
#include "tower_tablet_delete.h"
#include "tower_door_glow.h"
#include "phantom_sword_execute.h"
#include "phantom_maze.h"
#include "phantom_start.h"
#include "arrow_move.h"
#include "drc_pond_speed.h"
#include "drc_shortcut.h"
#include "entrance_safety.h"
#include "player_clothes_init.h"
#include "entrance_spawn.h"
#include "entrance_boat.h"

#include "damage_multiplier.h"

#include "swordless_walls.h"

#include "newgame_plus.h"
#include "boss_warp_demo.h"
#include "fairy_hint.h"
#include "hoho_spawn.h"
#include "hoho_execute.h"
#include "daily_mail.h"
#include "mailbox_init.h"
#include "hyrule_warp_init.h"
#include "pig_reward.h"
#include "legendary_photos.h"
#include "gallery_figures.h"
#include "npc_picto_visits.h"
#include "npc_replacement_cutscene_gates.h"
#include "bombshop_talk.h"
#include "bombshop_init.h"
#include "bombshop_create.h"
#include "bombshop_heap.h"
#include "moblin_create.h"
#include "moblin_execute.h"
#include "hyrule_warp_execute.h"
#include "molgera_camera.h"
#include "hyrule_barrier_gate.h"
#include "ganondorf_arrow_reflect.h"
#include "ganondorf_hit_sound.h"
#include "deku_chart_dialogue.h"
#include "korok_chart_icons.h"
#include "deku_chart_choices.h"

#include "post_puppet_layer.h"
#include "hyrule_enemy_layer.h"
#include "windfall_reward_grant.h"

#include "windfall_reward_queries.h"

#include "windfall_reward_status.h"

#include "windfall_reward_phases.h"

#include "windfall_reward_execute.h"

#include "windfall_reward_create.h"
#include "morth_delete.h"

#include "dampa_pending_pig.h"

#include "key_bag_text.h"

#include "poe_light_arrow.h"

#include "miniblin_light_arrow.h"
#include "warp_pot_smoke.h"
#include "instant_text.h"
#include "bluechu_hookshot_shock.h"

#include "aryll_pirate_outfit.h"
#include "ff_warp_basics.h"
#include "ff_warp_connections.h"
#include "ff_warp_child.h"
#include "ff_warp_statics.h"
#include "stalfos_light_arrow.h"

#include "orca_counter.h"
#include "rainbow_rupee.h"

#include "traversal_animation_small.h"
#include "traversal_seed_data.h"
#include "traversal_animation_hang.h"

#include "magtail_light_arrow.h"
#include "traversal_talk_camera.h"

#include "traversal_grapple.h"
#include "sea_compass.h"

#include "helmaroc_rng.h"
