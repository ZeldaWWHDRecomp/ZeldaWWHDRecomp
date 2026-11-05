/* SPDX-License-Identifier: MPL-2.0
 * Where each thing Game progress counts is found (game_progress.h): its square of the sea chart (1..49; 0 none fixed),
 * what it is, how it is found, and the save flag that says it was found. The map of the Game progress page lists them by
 * square. In the unchanged game each thing has one place of its own, so a set flag is "found there". Read only.
 *
 * From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (MPL-2.0), android/.../ProgressPlaces.java: its table with the
 * spots (stage, x / y / z) left out (only the square is kept), and its states(). Checked by game_progress_test:
 *  - against TWWHD Randomizer's vanilla data (SuperDude88 and contributors, MIT; THIRD-PARTY notice in
 *    docs/third-party/twwhd-randomizer.md): the square of every heart piece, chart and sunken treasure
 *    (location_data.yaml, area_names.yaml, roomNumToDefaultChart in logic/GameItem.cpp); the facts only, no file of it;
 *  - against real saves (WWHD_PROGRESS_SAVES): the found hearts, bottles and charts agree with the count.
 * Flags: a stage table's chest / item bit / dungeon item bit (dSv_memBit_c mTbox +0, mItem +0x14, mDungeonItem +0x21),
 * a square's ocean bit (dSv_ocean_c, save + 0x5C0, u16 per square), an event flag 0xBBMM (all of mask MM in event byte
 * BB), an event register (byte a, all of mask b; GE: (byte & b >> 8) >= (b & 0xFF)), a chart got / its treasure salvaged
 * (dSv_player_map_c sets 1 / 3), a square filled in; the rest are what the count already reads (items, songs...).
 * A WEAK flag (Maggie's letter mailed: the reward may come later) counts by the number of pieces: what the pieces
 * Link has leave over after the sure ones are "found, not sure which" (GP_MAYBE) among the weak ones not set.
 * The four bottles have no flags of their own but the submarine's chest and Beedle's sale; the others count in story
 * order.
 */
#ifndef WWHD_GAME_PROGRESS_PLACES_H
#define WWHD_GAME_PROGRESS_PLACES_H

#include "game_progress.h"

#ifdef __cplusplus
extern "C" {
#endif

/* what a place holds (its `what`: an item id for HEART / CONTAINER (7, 8), the dungeon's stage table entry 2..7
 * for MAP .. BOSS, a chart's save number 1..61 for CHART / SALVAGE, a square for SQUARE, an item id for ITEM, the
 * bow's got bit (2, 4) for ARROWS, a bottle 0..3, an equipment / upgrade index, a melody, a pearl bit, a shard 1..8) */
enum { GP_K_HEART, GP_K_CONTAINER, GP_K_MAP, GP_K_COMPASS, GP_K_BIGKEY, GP_K_BOSS, GP_K_CHART, GP_K_SALVAGE,
       GP_K_SQUARE, GP_K_ITEM, GP_K_ARROWS, GP_K_BOTTLE, GP_K_EQUIP, GP_K_UPGRADE, GP_K_SONG, GP_K_PEARL,
       GP_K_SHARD, GP_K_GALLERY, GP_KINDS };
enum { GP_F_CHEST, GP_F_ITEMBIT, GP_F_DUNGEON, GP_F_OCEAN, GP_F_EVENT, GP_F_REG, GP_F_REGGE, GP_F_CHART,
       GP_F_SALVAGED, GP_F_SQUARE, GP_F_ITEM, GP_F_ARROWS, GP_F_BOTTLE, GP_F_EQUIP, GP_F_UPGRADE, GP_F_SONG,
       GP_F_PEARL, GP_F_SHARD, GP_F_GALLERY };
/* how it is found (the list's words) */
enum { GP_S_CHEST, GP_S_SUB, GP_S_SUNKEN, GP_S_OCTO, GP_S_GUNBOAT, GP_S_GROUND, GP_S_REWARD, GP_S_AUCTION,
       GP_S_MINIGAME, GP_S_LETTER, GP_S_SHOP, GP_S_BOSS, GP_S_FISHMAN, GP_S_FAIRY, GP_S_UNKNOWN };
enum { GP_MISSING, GP_FOUND, GP_MAYBE };
/* the map's choice of things */
enum { GP_G_ALL, GP_G_HEARTS, GP_G_CHARTS, GP_G_ITEMS, GP_G_DUNGEONS };

typedef struct {
    unsigned char kind, what, square, flag;
    unsigned short a, b;
    unsigned char src, weak;
} gp_place;

static const gp_place gp_places[] = {
    {GP_K_HEART, 7, 49, GP_F_CHEST, 0xA, 0x1, GP_S_SUB, 0},
    {GP_K_HEART, 7, 25, GP_F_CHEST, 0xA, 0x0, GP_S_SUB, 0},
    {GP_K_HEART, 7, 34, GP_F_CHEST, 0xC, 0x5, GP_S_CHEST, 0},
    {GP_K_HEART, 7, 2, GP_F_CHEST, 0xC, 0x6, GP_S_CHEST, 0},
    {GP_K_HEART, 7, 12, GP_F_CHEST, 0xC, 0x1A, GP_S_CHEST, 0},
    {GP_K_HEART, 7, 1, GP_F_CHEST, 0x2, 0x3, GP_S_CHEST, 0},
    {GP_K_HEART, 7, 11, GP_F_CHEST, 0x0, 0xA, GP_S_CHEST, 0},
    {GP_K_HEART, 7, 23, GP_F_CHEST, 0x0, 0x6, GP_S_CHEST, 0},
    {GP_K_HEART, 7, 29, GP_F_CHEST, 0x0, 0x3, GP_S_CHEST, 0},
    {GP_K_HEART, 7, 31, GP_F_CHEST, 0x0, 0x14, GP_S_CHEST, 0},
    {GP_K_HEART, 7, 47, GP_F_CHEST, 0x0, 0x0, GP_S_CHEST, 0},
    {GP_K_HEART, 7, 46, GP_F_SALVAGED, 0x8, 0x0, GP_S_SUNKEN, 0},
    {GP_K_HEART, 7, 5, GP_F_SALVAGED, 0x9, 0x0, GP_S_SUNKEN, 0},
    {GP_K_HEART, 7, 47, GP_F_SALVAGED, 0xA, 0x0, GP_S_SUNKEN, 0},
    {GP_K_HEART, 7, 12, GP_F_SALVAGED, 0xB, 0x0, GP_S_SUNKEN, 0},
    {GP_K_HEART, 7, 34, GP_F_SALVAGED, 0xC, 0x0, GP_S_SUNKEN, 0},
    {GP_K_HEART, 7, 28, GP_F_SALVAGED, 0xD, 0x0, GP_S_SUNKEN, 0},
    {GP_K_HEART, 7, 36, GP_F_SALVAGED, 0xE, 0x0, GP_S_SUNKEN, 0},
    {GP_K_HEART, 7, 41, GP_F_SALVAGED, 0xF, 0x0, GP_S_SUNKEN, 0},
    {GP_K_HEART, 7, 49, GP_F_SALVAGED, 0x10, 0x0, GP_S_SUNKEN, 0},
    {GP_K_HEART, 7, 16, GP_F_SALVAGED, 0x11, 0x0, GP_S_SUNKEN, 0},
    {GP_K_HEART, 7, 22, GP_F_SALVAGED, 0x12, 0x0, GP_S_SUNKEN, 0},
    {GP_K_HEART, 7, 39, GP_F_SALVAGED, 0x1E, 0x0, GP_S_SUNKEN, 0},
    {GP_K_HEART, 7, 6, GP_F_OCEAN, 0x6, 0x0, GP_S_OCTO, 0},
    {GP_K_HEART, 7, 16, GP_F_OCEAN, 0x10, 0x0, GP_S_GUNBOAT, 0},
    {GP_K_HEART, 7, 17, GP_F_OCEAN, 0x11, 0x0, GP_S_OCTO, 0},
    {GP_K_HEART, 7, 44, GP_F_ITEMBIT, 0x0, 0x2, GP_S_GROUND, 0},
    {GP_K_HEART, 7, 45, GP_F_ITEMBIT, 0x0, 0x8, GP_S_GROUND, 0},
    {GP_K_HEART, 7, 44, GP_F_REGGE, 0xD0, 0xFF03, GP_S_REWARD, 0},
    {GP_K_HEART, 7, 11, GP_F_EVENT, 0x1340, 0x0, GP_S_REWARD, 0},
    {GP_K_HEART, 7, 11, GP_F_EVENT, 0x1220, 0x0, GP_S_REWARD, 1},
    {GP_K_HEART, 7, 11, GP_F_EVENT, 0x1B20, 0x0, GP_S_REWARD, 0},
    {GP_K_HEART, 7, 11, GP_F_EVENT, 0x1020, 0x0, GP_S_AUCTION, 0},
    {GP_K_HEART, 7, 11, GP_F_EVENT, 0x1B10, 0x0, GP_S_REWARD, 0},
    {GP_K_HEART, 7, 11, GP_F_REGGE, 0xFE, 0x701, GP_S_MINIGAME, 0},
    {GP_K_HEART, 7, 11, GP_F_EVENT, 0x2280, 0x0, GP_S_REWARD, 0},
    {GP_K_HEART, 7, 13, GP_F_REG, 0xAE, 0x3, GP_S_LETTER, 0},
    {GP_K_HEART, 7, 13, GP_F_REG, 0xAC, 0x3, GP_S_LETTER, 0},
    {GP_K_HEART, 7, 13, GP_F_REG, 0xB5, 0x3, GP_S_LETTER, 0},
    {GP_K_HEART, 7, 23, GP_F_EVENT, 0x3E04, 0x0, GP_S_REWARD, 0},
    {GP_K_HEART, 7, 41, GP_F_EVENT, 0x102, 0x0, GP_S_REWARD, 0},
    {GP_K_HEART, 7, 10, GP_F_REGGE, 0xB7, 0x301, GP_S_MINIGAME, 0},
    {GP_K_HEART, 7, 16, GP_F_EVENT, 0x2010, 0x0, GP_S_SHOP, 0},
    {GP_K_HEART, 7, 14, GP_F_EVENT, 0x2B40, 0x0, GP_S_MINIGAME, 0},
    {GP_K_MAP, 2, 1, GP_F_DUNGEON, 0x2, 0x0, GP_S_CHEST, 0},
    {GP_K_COMPASS, 2, 1, GP_F_DUNGEON, 0x2, 0x1, GP_S_CHEST, 0},
    {GP_K_BOSS, 2, 1, GP_F_DUNGEON, 0x2, 0x3, GP_S_BOSS, 0},
    {GP_K_CONTAINER, 2, 1, GP_F_DUNGEON, 0x2, 0x4, GP_S_BOSS, 0},
    {GP_K_MAP, 3, 13, GP_F_DUNGEON, 0x3, 0x0, GP_S_CHEST, 0},
    {GP_K_COMPASS, 3, 13, GP_F_DUNGEON, 0x3, 0x1, GP_S_CHEST, 0},
    {GP_K_BIGKEY, 3, 13, GP_F_DUNGEON, 0x3, 0x2, GP_S_CHEST, 0},
    {GP_K_BOSS, 3, 13, GP_F_DUNGEON, 0x3, 0x3, GP_S_BOSS, 0},
    {GP_K_CONTAINER, 3, 13, GP_F_DUNGEON, 0x3, 0x4, GP_S_BOSS, 0},
    {GP_K_MAP, 4, 41, GP_F_DUNGEON, 0x4, 0x0, GP_S_CHEST, 0},
    {GP_K_COMPASS, 4, 41, GP_F_DUNGEON, 0x4, 0x1, GP_S_CHEST, 0},
    {GP_K_BIGKEY, 4, 41, GP_F_DUNGEON, 0x4, 0x2, GP_S_CHEST, 0},
    {GP_K_BOSS, 4, 41, GP_F_DUNGEON, 0x4, 0x3, GP_S_BOSS, 0},
    {GP_K_CONTAINER, 4, 41, GP_F_DUNGEON, 0x4, 0x4, GP_S_BOSS, 0},
    {GP_K_MAP, 5, 26, GP_F_DUNGEON, 0x5, 0x0, GP_S_CHEST, 0},
    {GP_K_COMPASS, 5, 26, GP_F_DUNGEON, 0x5, 0x1, GP_S_CHEST, 0},
    {GP_K_BIGKEY, 5, 26, GP_F_DUNGEON, 0x5, 0x2, GP_S_CHEST, 0},
    {GP_K_BOSS, 5, 26, GP_F_DUNGEON, 0x5, 0x3, GP_S_BOSS, 0},
    {GP_K_CONTAINER, 5, 26, GP_F_DUNGEON, 0x5, 0x4, GP_S_BOSS, 0},
    {GP_K_MAP, 6, 45, GP_F_DUNGEON, 0x6, 0x0, GP_S_CHEST, 0},
    {GP_K_COMPASS, 6, 45, GP_F_DUNGEON, 0x6, 0x1, GP_S_CHEST, 0},
    {GP_K_BIGKEY, 6, 45, GP_F_DUNGEON, 0x6, 0x2, GP_S_CHEST, 0},
    {GP_K_BOSS, 6, 45, GP_F_DUNGEON, 0x6, 0x3, GP_S_BOSS, 0},
    {GP_K_CONTAINER, 6, 45, GP_F_DUNGEON, 0x6, 0x4, GP_S_BOSS, 0},
    {GP_K_MAP, 7, 4, GP_F_DUNGEON, 0x7, 0x0, GP_S_CHEST, 0},
    {GP_K_COMPASS, 7, 4, GP_F_DUNGEON, 0x7, 0x1, GP_S_CHEST, 0},
    {GP_K_BIGKEY, 7, 4, GP_F_DUNGEON, 0x7, 0x2, GP_S_CHEST, 0},
    {GP_K_BOSS, 7, 4, GP_F_DUNGEON, 0x7, 0x3, GP_S_BOSS, 0},
    {GP_K_CONTAINER, 7, 4, GP_F_DUNGEON, 0x7, 0x4, GP_S_BOSS, 0},
    {GP_K_CHART, 1, 30, GP_F_CHART, 0x1, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 2, 11, GP_F_CHART, 0x2, 0x0, GP_S_MINIGAME, 0},
    {GP_K_CHART, 3, 35, GP_F_CHART, 0x3, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 4, 43, GP_F_CHART, 0x4, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 5, 29, GP_F_CHART, 0x5, 0x0, GP_S_GUNBOAT, 0},
    {GP_K_CHART, 6, 21, GP_F_CHART, 0x6, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 7, 11, GP_F_CHART, 0x7, 0x0, GP_S_REWARD, 0},
    {GP_K_CHART, 8, 12, GP_F_CHART, 0x8, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 9, 13, GP_F_CHART, 0x9, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 10, 41, GP_F_CHART, 0xA, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 11, 26, GP_F_CHART, 0xB, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 12, 45, GP_F_CHART, 0xC, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 13, 4, GP_F_CHART, 0xD, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 14, 11, GP_F_CHART, 0xE, 0x0, GP_S_MINIGAME, 0},
    {GP_K_CHART, 15, 11, GP_F_CHART, 0xF, 0x0, GP_S_REWARD, 0},
    {GP_K_CHART, 16, 11, GP_F_CHART, 0x10, 0x0, GP_S_REWARD, 0},
    {GP_K_CHART, 17, 11, GP_F_CHART, 0x11, 0x0, GP_S_REWARD, 0},
    {GP_K_CHART, 18, 11, GP_F_CHART, 0x12, 0x0, GP_S_AUCTION, 0},
    {GP_K_CHART, 19, 13, GP_F_CHART, 0x13, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 20, 11, GP_F_CHART, 0x14, 0x0, GP_S_REWARD, 0},
    {GP_K_CHART, 21, 26, GP_F_CHART, 0x15, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 22, 45, GP_F_CHART, 0x16, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 23, 4, GP_F_CHART, 0x17, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 24, 41, GP_F_CHART, 0x18, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 25, 11, GP_F_CHART, 0x19, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 26, 0, GP_F_CHART, 0x1A, 0x0, GP_S_REWARD, 0},
    {GP_K_CHART, 27, 11, GP_F_CHART, 0x1B, 0x0, GP_S_AUCTION, 0},
    {GP_K_CHART, 28, 6, GP_F_CHART, 0x1C, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 29, 43, GP_F_CHART, 0x1D, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 30, 16, GP_F_CHART, 0x1E, 0x0, GP_S_SHOP, 0},
    {GP_K_CHART, 31, 41, GP_F_CHART, 0x1F, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 32, 39, GP_F_CHART, 0x20, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 33, 5, GP_F_CHART, 0x21, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 34, 45, GP_F_CHART, 0x22, 0x0, GP_S_SUB, 0},
    {GP_K_CHART, 37, 5, GP_F_CHART, 0x25, 0x0, GP_S_SUB, 0},
    {GP_K_CHART, 38, 3, GP_F_CHART, 0x26, 0x0, GP_S_SUB, 0},
    {GP_K_CHART, 39, 40, GP_F_CHART, 0x27, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 40, 10, GP_F_CHART, 0x28, 0x0, GP_S_MINIGAME, 0},
    {GP_K_CHART, 41, 42, GP_F_CHART, 0x29, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 42, 16, GP_F_CHART, 0x2A, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 43, 43, GP_F_CHART, 0x2B, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 44, 25, GP_F_CHART, 0x2C, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 45, 37, GP_F_CHART, 0x2D, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 46, 8, GP_F_CHART, 0x2E, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 47, 22, GP_F_CHART, 0x2F, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 48, 46, GP_F_CHART, 0x30, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 49, 24, GP_F_CHART, 0x31, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 50, 33, GP_F_CHART, 0x32, 0x0, GP_S_CHEST, 0},
    {GP_K_CHART, 51, 11, GP_F_CHART, 0x33, 0x0, GP_S_MINIGAME, 0},
    {GP_K_SALVAGE, 1, 23, GP_F_SALVAGED, 0x1, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 2, 4, GP_F_SALVAGED, 0x2, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 3, 31, GP_F_SALVAGED, 0x3, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 4, 44, GP_F_SALVAGED, 0x4, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 5, 42, GP_F_SALVAGED, 0x5, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 6, 32, GP_F_SALVAGED, 0x6, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 7, 6, GP_F_SALVAGED, 0x7, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 8, 46, GP_F_SALVAGED, 0x8, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 9, 5, GP_F_SALVAGED, 0x9, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 10, 47, GP_F_SALVAGED, 0xA, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 11, 12, GP_F_SALVAGED, 0xB, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 12, 34, GP_F_SALVAGED, 0xC, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 13, 28, GP_F_SALVAGED, 0xD, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 14, 36, GP_F_SALVAGED, 0xE, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 15, 41, GP_F_SALVAGED, 0xF, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 16, 49, GP_F_SALVAGED, 0x10, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 17, 16, GP_F_SALVAGED, 0x11, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 18, 22, GP_F_SALVAGED, 0x12, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 19, 13, GP_F_SALVAGED, 0x13, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 20, 3, GP_F_SALVAGED, 0x14, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 21, 25, GP_F_SALVAGED, 0x15, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 22, 37, GP_F_SALVAGED, 0x16, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 23, 30, GP_F_SALVAGED, 0x17, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 24, 33, GP_F_SALVAGED, 0x18, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 25, 9, GP_F_SALVAGED, 0x19, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 26, 27, GP_F_SALVAGED, 0x1A, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 27, 11, GP_F_SALVAGED, 0x1B, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 28, 38, GP_F_SALVAGED, 0x1C, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 29, 29, GP_F_SALVAGED, 0x1D, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 30, 39, GP_F_SALVAGED, 0x1E, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 31, 19, GP_F_SALVAGED, 0x1F, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 32, 45, GP_F_SALVAGED, 0x20, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 33, 17, GP_F_SALVAGED, 0x21, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 34, 26, GP_F_SALVAGED, 0x22, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 37, 43, GP_F_SALVAGED, 0x25, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 38, 10, GP_F_SALVAGED, 0x26, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 39, 35, GP_F_SALVAGED, 0x27, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 40, 40, GP_F_SALVAGED, 0x28, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 41, 1, GP_F_SALVAGED, 0x29, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 42, 20, GP_F_SALVAGED, 0x2A, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 43, 15, GP_F_SALVAGED, 0x2B, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 44, 18, GP_F_SALVAGED, 0x2C, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 45, 8, GP_F_SALVAGED, 0x2D, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 46, 14, GP_F_SALVAGED, 0x2E, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 47, 48, GP_F_SALVAGED, 0x2F, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 48, 7, GP_F_SALVAGED, 0x30, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 49, 24, GP_F_SALVAGED, 0x31, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 50, 21, GP_F_SALVAGED, 0x32, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SALVAGE, 51, 2, GP_F_SALVAGED, 0x33, 0x0, GP_S_SUNKEN, 0},
    {GP_K_SQUARE, 1, 1, GP_F_SQUARE, 0x1, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 2, 2, GP_F_SQUARE, 0x2, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 3, 3, GP_F_SQUARE, 0x3, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 4, 4, GP_F_SQUARE, 0x4, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 5, 5, GP_F_SQUARE, 0x5, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 6, 6, GP_F_SQUARE, 0x6, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 7, 7, GP_F_SQUARE, 0x7, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 8, 8, GP_F_SQUARE, 0x8, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 9, 9, GP_F_SQUARE, 0x9, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 10, 10, GP_F_SQUARE, 0xA, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 11, 11, GP_F_SQUARE, 0xB, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 12, 12, GP_F_SQUARE, 0xC, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 13, 13, GP_F_SQUARE, 0xD, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 14, 14, GP_F_SQUARE, 0xE, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 15, 15, GP_F_SQUARE, 0xF, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 16, 16, GP_F_SQUARE, 0x10, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 17, 17, GP_F_SQUARE, 0x11, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 18, 18, GP_F_SQUARE, 0x12, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 19, 19, GP_F_SQUARE, 0x13, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 20, 20, GP_F_SQUARE, 0x14, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 21, 21, GP_F_SQUARE, 0x15, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 22, 22, GP_F_SQUARE, 0x16, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 23, 23, GP_F_SQUARE, 0x17, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 24, 24, GP_F_SQUARE, 0x18, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 25, 25, GP_F_SQUARE, 0x19, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 26, 26, GP_F_SQUARE, 0x1A, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 27, 27, GP_F_SQUARE, 0x1B, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 28, 28, GP_F_SQUARE, 0x1C, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 29, 29, GP_F_SQUARE, 0x1D, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 30, 30, GP_F_SQUARE, 0x1E, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 31, 31, GP_F_SQUARE, 0x1F, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 32, 32, GP_F_SQUARE, 0x20, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 33, 33, GP_F_SQUARE, 0x21, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 34, 34, GP_F_SQUARE, 0x22, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 35, 35, GP_F_SQUARE, 0x23, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 36, 36, GP_F_SQUARE, 0x24, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 37, 37, GP_F_SQUARE, 0x25, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 38, 38, GP_F_SQUARE, 0x26, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 39, 39, GP_F_SQUARE, 0x27, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 40, 40, GP_F_SQUARE, 0x28, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 41, 41, GP_F_SQUARE, 0x29, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 42, 42, GP_F_SQUARE, 0x2A, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 43, 43, GP_F_SQUARE, 0x2B, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 44, 44, GP_F_SQUARE, 0x2C, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 45, 45, GP_F_SQUARE, 0x2D, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 46, 46, GP_F_SQUARE, 0x2E, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 47, 47, GP_F_SQUARE, 0x2F, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 48, 48, GP_F_SQUARE, 0x30, 0x0, GP_S_FISHMAN, 0},
    {GP_K_SQUARE, 49, 49, GP_F_SQUARE, 0x31, 0x0, GP_S_FISHMAN, 0},
    {GP_K_ITEM, 32, 44, GP_F_ITEM, 0x20, 0x0, GP_S_REWARD, 0},
    {GP_K_ITEM, 120, 11, GP_F_ITEM, 0x78, 0x0, GP_S_SHOP, 0},
    {GP_K_ITEM, 119, 11, GP_F_ITEM, 0x77, 0x0, GP_S_AUCTION, 0},
    {GP_K_ITEM, 34, 13, GP_F_ITEM, 0x22, 0x0, GP_S_REWARD, 0},
    {GP_K_ITEM, 37, 13, GP_F_ITEM, 0x25, 0x0, GP_S_CHEST, 0},
    {GP_K_ITEM, 36, 11, GP_F_ITEM, 0x24, 0x0, GP_S_CHEST, 0},
    {GP_K_ITEM, 45, 41, GP_F_ITEM, 0x2D, 0x0, GP_S_CHEST, 0},
    {GP_K_ITEM, 52, 41, GP_F_ITEM, 0x34, 0x0, GP_S_GROUND, 0},
    {GP_K_ITEM, 33, 11, GP_F_ITEM, 0x21, 0x0, GP_S_REWARD, 0},
    {GP_K_ITEM, 35, 11, GP_F_ITEM, 0x23, 0x0, GP_S_CHEST, 0},
    {GP_K_ITEM, 38, 11, GP_F_ITEM, 0x26, 0x0, GP_S_REWARD, 0},
    {GP_K_ITEM, 41, 40, GP_F_ITEM, 0x29, 0x0, GP_S_CHEST, 0},
    {GP_K_ITEM, 42, 11, GP_F_ITEM, 0x2A, 0x0, GP_S_SHOP, 0},
    {GP_K_ITEM, 44, 11, GP_F_ITEM, 0x2C, 0x0, GP_S_SHOP, 0},
    {GP_K_ITEM, 39, 26, GP_F_ITEM, 0x27, 0x0, GP_S_CHEST, 0},
    {GP_K_ITEM, 49, 11, GP_F_ITEM, 0x31, 0x0, GP_S_CHEST, 0},
    {GP_K_ITEM, 48, 13, GP_F_ITEM, 0x30, 0x0, GP_S_REWARD, 0},
    {GP_K_ITEM, 47, 4, GP_F_ITEM, 0x2F, 0x0, GP_S_CHEST, 0},
    {GP_K_ITEM, 51, 1, GP_F_ITEM, 0x33, 0x0, GP_S_CHEST, 0},
    {GP_K_ARROWS, 2, 9, GP_F_ARROWS, 0x2, 0x0, GP_S_FAIRY, 0},
    {GP_K_ARROWS, 4, 26, GP_F_ARROWS, 0x4, 0x0, GP_S_CHEST, 0},
    {GP_K_BOTTLE, 0, 13, GP_F_BOTTLE, 0x0, 0x0, GP_S_REWARD, 0},
    {GP_K_BOTTLE, 1, 11, GP_F_BOTTLE, 0x1, 0x0, GP_S_REWARD, 0},
    {GP_K_BOTTLE, 2, 34, GP_F_BOTTLE, 0x2, 0x0, GP_S_SUB, 0},
    {GP_K_BOTTLE, 3, 16, GP_F_BOTTLE, 0x3, 0x0, GP_S_SHOP, 0},
    {GP_K_EQUIP, 0, 44, GP_F_EQUIP, 0x0, 0x0, GP_S_REWARD, 0},
    {GP_K_EQUIP, 1, 26, GP_F_EQUIP, 0x1, 0x0, GP_S_GROUND, 0},
    {GP_K_EQUIP, 2, 45, GP_F_EQUIP, 0x2, 0x0, GP_S_BOSS, 0},
    {GP_K_EQUIP, 3, 4, GP_F_EQUIP, 0x3, 0x0, GP_S_BOSS, 0},
    {GP_K_EQUIP, 4, 44, GP_F_EQUIP, 0x4, 0x0, GP_S_REWARD, 0},
    {GP_K_EQUIP, 5, 45, GP_F_EQUIP, 0x5, 0x0, GP_S_CHEST, 0},
    {GP_K_EQUIP, 6, 20, GP_F_EQUIP, 0x6, 0x0, GP_S_CHEST, 0},
    {GP_K_EQUIP, 7, 44, GP_F_EQUIP, 0x7, 0x0, GP_S_REWARD, 0},
    {GP_K_EQUIP, 8, 44, GP_F_EQUIP, 0x8, 0x0, GP_S_CHEST, 0},
    {GP_K_UPGRADE, 0, 3, GP_F_UPGRADE, 0x0, 0x0, GP_S_FAIRY, 0},
    {GP_K_UPGRADE, 1, 44, GP_F_UPGRADE, 0x1, 0x0, GP_S_FAIRY, 0},
    {GP_K_UPGRADE, 2, 19, GP_F_UPGRADE, 0x2, 0x0, GP_S_FAIRY, 0},
    {GP_K_UPGRADE, 3, 39, GP_F_UPGRADE, 0x3, 0x0, GP_S_FAIRY, 0},
    {GP_K_UPGRADE, 4, 15, GP_F_UPGRADE, 0x4, 0x0, GP_S_FAIRY, 0},
    {GP_K_UPGRADE, 5, 28, GP_F_UPGRADE, 0x5, 0x0, GP_S_FAIRY, 0},
    {GP_K_UPGRADE, 6, 46, GP_F_UPGRADE, 0x6, 0x0, GP_S_FAIRY, 0},
    {GP_K_SONG, 0, 13, GP_F_SONG, 0x0, 0x0, GP_S_REWARD, 0},
    {GP_K_SONG, 1, 24, GP_F_SONG, 0x1, 0x0, GP_S_REWARD, 0},
    {GP_K_SONG, 2, 26, GP_F_SONG, 0x2, 0x0, GP_S_REWARD, 0},
    {GP_K_SONG, 3, 45, GP_F_SONG, 0x3, 0x0, GP_S_REWARD, 0},
    {GP_K_SONG, 4, 4, GP_F_SONG, 0x4, 0x0, GP_S_REWARD, 0},
    {GP_K_SONG, 5, 11, GP_F_SONG, 0x5, 0x0, GP_S_REWARD, 0},
    {GP_K_PEARL, 1, 13, GP_F_PEARL, 0x1, 0x0, GP_S_REWARD, 0},
    {GP_K_PEARL, 2, 41, GP_F_PEARL, 0x2, 0x0, GP_S_REWARD, 0},
    {GP_K_PEARL, 0, 44, GP_F_PEARL, 0x0, 0x0, GP_S_REWARD, 0},
    {GP_K_SHARD, 1, 23, GP_F_SHARD, 0x1, 0x1, GP_S_SUNKEN, 0},
    {GP_K_SHARD, 2, 33, GP_F_SHARD, 0x2, 0x0, GP_S_CHEST, 0},
    {GP_K_SHARD, 3, 31, GP_F_SHARD, 0x3, 0x3, GP_S_SUNKEN, 0},
    {GP_K_SHARD, 4, 0, GP_F_SHARD, 0x4, 0x0, GP_S_CHEST, 0},
    {GP_K_SHARD, 5, 42, GP_F_SHARD, 0x5, 0x5, GP_S_SUNKEN, 0},
    {GP_K_SHARD, 6, 44, GP_F_SHARD, 0x6, 0x0, GP_S_CHEST, 0},
    {GP_K_SHARD, 7, 31, GP_F_SHARD, 0x7, 0x0, GP_S_CHEST, 0},
    {GP_K_SHARD, 8, 7, GP_F_SHARD, 0x8, 0x0, GP_S_CHEST, 0},
    {GP_K_GALLERY, 0, 11, GP_F_GALLERY, 0x0, 0x0, GP_S_REWARD, 0},
};
enum { GP_PLACES = sizeof gp_places / sizeof gp_places[0] };

static inline int gp_place_in_group(const gp_place* p, int group) {
    switch (group) {
    case GP_G_HEARTS: return p->kind == GP_K_HEART || p->kind == GP_K_CONTAINER;
    case GP_G_CHARTS: return p->kind == GP_K_CHART || p->kind == GP_K_SALVAGE || p->kind == GP_K_SQUARE;
    case GP_G_ITEMS: return p->kind >= GP_K_ITEM;
    case GP_G_DUNGEONS: return (p->kind >= GP_K_MAP && p->kind <= GP_K_BOSS) || p->kind == GP_K_CONTAINER;
    default: return 1;
    }
}

/* bit b of a big-endian u32 at a stage's memBit offset o (mTbox +0, mItem +0x14) */
static inline int gp_stage_bit32(const unsigned char* d, int stage, unsigned o, unsigned b) {
    return (gp_stage_byte(d, stage, o + 3 - b / 8) >> (b % 8)) & 1;
}

static inline int gp_item_slot(unsigned id) {
    switch (id) {
    case 0x20: return 0;
    case 0x78: case 0x77: return 1;
    case 0x22: return 2;
    case 0x25: return 3;
    case 0x24: return 4;
    case 0x2D: return 5;
    case 0x34: return 6;
    case 0x21: return 7;
    case 0x23: case 0x26: return 8;
    case 0x29: return 9;
    case 0x2A: return 10;
    case 0x2C: return 11;
    case 0x27: return 12;
    case 0x31: return 13;
    case 0x30: return 18;
    case 0x2F: return 19;
    case 0x33: return 20;
    default: return -1;
    }
}

/* whether the place's own flag is set (bottles: see gp_place_states) */
static inline int gp_place_flag(const unsigned char* d, const gp_place* p) {
    static const unsigned char equip[9][2] = {{0, 1}, {0, 2}, {0, 4}, {0, 8}, {1, 1}, {1, 2}, {2, 1}, {3, 1}, {4, 1}};
    unsigned v, f, n;
    int s;
    switch (p->flag) {
    case GP_F_CHEST: return gp_stage_bit32(d, p->a, 0, p->b);
    case GP_F_ITEMBIT: return gp_stage_bit32(d, p->a, 0x14, p->b);
    case GP_F_DUNGEON: return (gp_stage_byte(d, p->a, GP_DUNGEON_ITEM) >> p->b) & 1;
    case GP_F_OCEAN: return (((unsigned)d[GP_OCEAN + 2 * p->a] << 8 | d[GP_OCEAN + 2 * p->a + 1]) >> p->b) & 1;
    case GP_F_EVENT: return (d[GP_EVENTS + (p->a >> 8)] & (p->a & 0xFF)) == (p->a & 0xFF);
    case GP_F_REG: return (d[GP_EVENTS + p->a] & p->b) == p->b;
    case GP_F_REGGE: return (d[GP_EVENTS + p->a] & (p->b >> 8)) >= (p->b & 0xFF);
    case GP_F_CHART: return gp_chart(d, 1, p->a);
    case GP_F_SALVAGED: return gp_chart(d, 3, p->a);
    case GP_F_SQUARE: return d[GP_FMAP + p->a - 1] & 1;
    case GP_F_ITEM:
        s = gp_item_slot(p->a);
        if (s < 0) return 0;
        v = d[GP_ITEMS + s];
        return p->a == 0x77 || p->a == 0x26 ? v == p->a : v != 0xFF;
    case GP_F_ARROWS:
        v = d[GP_ITEMS + 12];
        return v != 0xFF && ((d[GP_GOT_ITEMS + 12] & p->a) || v >= (p->a == 2 ? 0x35u : 0x36u));
    case GP_F_EQUIP: return p->a < 9 && (d[GP_COLLECT + equip[p->a][0]] & equip[p->a][1]) != 0;
    case GP_F_UPGRADE:
        switch (p->a) {
        case 0: return d[GP_WALLET] >= 1;
        case 1: return d[GP_WALLET] >= 2;
        case 2: return d[GP_BOMB_MAX] >= 60;
        case 3: return d[GP_BOMB_MAX] >= 99;
        case 4: return d[GP_ARROW_MAX] >= 60;
        case 5: return d[GP_ARROW_MAX] >= 99;
        default: return d[GP_MAX_MAGIC] >= 32;
        }
    case GP_F_SONG: return (d[GP_TACT] >> p->a) & 1;
    case GP_F_PEARL: return (d[GP_PEARLS] >> p->a) & 1;
    case GP_F_SHARD: return (d[GP_TRIFORCE] >> (p->a - 1)) & 1;
    case GP_F_GALLERY:
        for (f = 0, n = 0; f < GP_FIGURINES; f++) n += (unsigned)gp_figurine(d, f);
        return n >= GP_FIGURINES;
    default: return 0;
    }
}

/* each place's state (GP_MISSING, GP_FOUND, GP_MAYBE) in the save data d, by index into gp_places */
static inline void gp_place_states(const unsigned char* d, unsigned char states[GP_PLACES]) {
    unsigned i, containers = 0, sure = 0, life, pieces, bottles = 0, left;
    int extra, sub, beedle;
    for (i = 0; i < GP_PLACES; i++) {
        const int f = gp_place_flag(d, &gp_places[i]);
        states[i] = f ? GP_FOUND : GP_MISSING;
        if (gp_places[i].kind == GP_K_CONTAINER && f) containers++;
        if (gp_places[i].kind == GP_K_HEART && f) sure++;
    }
    life = ((unsigned)d[GP_MAX_LIFE] << 8) | d[GP_MAX_LIFE + 1];
    pieces = life > 12 + 4 * containers ? life - 12 - 4 * containers : 0;
    extra = (int)pieces - (int)sure;
    for (i = 0; i < GP_PLACES && extra > 0; i++)
        if (gp_places[i].weak && states[i] == GP_MISSING) states[i] = GP_MAYBE, extra--;
    /* the bottles: Bomb Island's submarine (its chest, table 10 bit 2) and Beedle's bottle (event 0x2020) */
    for (i = 14; i < 18; i++) bottles += d[GP_ITEMS + i] != 0xFF;
    sub = gp_stage_bit32(d, 10, 0, 2);
    beedle = (d[GP_EVENTS + 0x20] & 0x20) != 0;
    left = bottles > (unsigned)(sub + beedle) ? bottles - (unsigned)(sub + beedle) : 0;
    for (i = 0; i < GP_PLACES; i++) {
        const gp_place* p = &gp_places[i];
        int f;
        if (p->flag != GP_F_BOTTLE) continue;
        if (p->a == 2) f = sub;
        else if (p->a == 3) f = beedle;
        else f = left ? (left--, 1) : 0;
        states[i] = f ? GP_FOUND : GP_MISSING;
    }
}

#ifdef __cplusplus
}
#endif
#endif
