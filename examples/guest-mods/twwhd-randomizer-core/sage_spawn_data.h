/* Static spawn metadata from MIT TWWHD Randomizer 1.2.0 make_game_nonlinear.asm.
 * Copyright (c) 2024 SuperDude88. See LICENSE-TWWHD-Randomizer.txt.
 * Seven records per companion: spawn:u8, room:u8, rotation:s16, xyz:f32.
 * Integer position words preserve the assembler's exact binary32 data. */
#pragma once
static const unsigned int randomizer_sage_makar_spawns[7][4] = {
    {0x0F0F94A0u,0xC5643052u,0x44C2B571u,0x464ECCCDu},
    {0x16004000u,0xC58322A4u,0x443CA127u,0x45E8C400u},
    {0x1702B000u,0x442706D9u,0x44C1C000u,0x450FAC00u},
    {0x180CC000u,0x465DEC66u,0xC59E33ECu,0x460BD033u},
    {0x45034000u,0xC5819533u,0x44898000u,0x423F851Fu},
    {0x14020000u,0xC2DE947Bu,0xC5610000u,0xC4DA747Bu},
    {0x460C0000u,0x465C5000u,0xC59E33ECu,0x46115000u},
};
static const unsigned int randomizer_sage_medli_spawns[7][4] = {
    {0x00008000u,0xC5E179AEu,0xC3480000u,0x45A45652u},
    {0x1602E000u,0xC4FBA385u,0x43480000u,0xC49DDF0Au},
    {0x17068000u,0x45947000u,0x43AF0000u,0xC50CB0F6u},
    {0x180FE000u,0xC5143614u,0xC4FA0000u,0x46045E29u},
    {0x45010000u,0xC5FA5000u,0x447A0000u,0xC4BC9E14u},
    {0x09078000u,0x45C4E87Bu,0x44AF0000u,0x451334A4u},
    {0x110F8000u,0xC5B4A000u,0xC50FC000u,0x46179000u},
};
