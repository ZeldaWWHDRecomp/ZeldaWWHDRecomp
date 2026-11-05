// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.zip.CRC32;

/**
 * A copy of the game's cking.sav for a bug report, with the player's names replaced by "Link" (Share log with the
 * save files, LogShare). The file's own checks are made again, so the copy loads in the game like the original.
 * Returns null for anything that is not a cking.sav the game wrote (wrong size, a slot sum, the format word or the
 * CRC not matching): such a file may hold anything and is left out of the report.
 *
 * Layout (tools/savegame/wwsave.py, from the verified WWHD decompilation): three slots of 0xA94 bytes, each the
 * packed save data with its byte sum and complement sum at +0xA8C / +0xA90; then the HD-only sections, each for the
 * three slots (player 16, status 4, event 20, name 18, map 220 bytes), the format word 4 and the CRC-32 of those
 * sections, in which each name counts only (characters + 1) bytes. The packed data's names are the info block's
 * (+0x143) three 17-byte names at +0x14, +0x25 and +0x36.
 *
 * From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (MPL-2.0), GcSave.withoutNames; with the checks of
 * the file added.
 */
final class SaveScrub {
    private SaveScrub() {}

    static final int HD_SLOT = 0xA94, HD_SLOT_USED = 0xA8C, HD_FILE = 8966, FORMAT_WORD = 4;
    private static final int[] HD_EXTRA = {16, 4, 20, 18, 220};  // player, status, event, name, map
    private static final int NAME_SECTION = 3;
    private static final int INFO = 0x143;
    private static final int[] NAME_FIELDS = {0x14, 0x25, 0x36};
    static final String NAME = "Link";

    /** the copy with every name "Link", or null when `sav` is not a valid cking.sav. The port's story block after
     *  the game's bytes (runtime/src/mods/story_block.h: payload, then the 16-byte footer "WSTB", payload length, ...)
     *  is left out: it can hold the whole name (long_name.h) and the story mods' records. */
    static byte[] withoutNames(byte[] sav) {
        if (sav == null) return null;
        sav = gamePart(sav);
        if (!valid(sav)) return null;
        byte[] out = sav.clone();
        for (int s = 0; s < 3; s++) {
            int o = s * HD_SLOT;
            for (int f : NAME_FIELDS) {
                if (out[o + INFO + f] == 0) continue;  // (an unused name stays empty: an empty file stays empty)
                Arrays.fill(out, o + INFO + f, o + INFO + f + 17, (byte) 0);
                byte[] link = NAME.getBytes(StandardCharsets.US_ASCII);
                System.arraycopy(link, 0, out, o + INFO + f, link.length);
            }
            putSums(out, o);
        }
        int p = namesAt();
        for (int s = 0; s < 3; s++, p += HD_EXTRA[NAME_SECTION]) {
            if (out[p] == 0 && out[p + 1] == 0) continue;
            Arrays.fill(out, p, p + 18, (byte) 0);
            byte[] link = NAME.getBytes(StandardCharsets.UTF_16BE);
            System.arraycopy(link, 0, out, p, link.length);
        }
        put32(out, crcAt(), crc(out));
        return out;
    }

    /** the game's part of a cking.sav that may carry the port's story block (the file itself without one) */
    static byte[] gamePart(byte[] sav) {
        final int footer = 16;
        if (sav.length <= HD_FILE || sav.length < HD_FILE + footer) return sav;
        int f = sav.length - footer;
        if (sav[f] != 'W' || sav[f + 1] != 'S' || sav[f + 2] != 'T' || sav[f + 3] != 'B') return sav;
        long length = get32(sav, f + 4);
        if (length > f || f - length != HD_FILE) return sav;
        return Arrays.copyOf(sav, HD_FILE);
    }

    /** whether `sav` is a cking.sav as the game writes it: size, each slot's sums, the format word and the CRC */
    static boolean valid(byte[] sav) {
        if (sav.length != HD_FILE) return false;
        for (int s = 0; s < 3; s++) {
            long[] sums = sums(sav, s * HD_SLOT);
            if (get32(sav, s * HD_SLOT + HD_SLOT_USED) != sums[0] || get32(sav, s * HD_SLOT + HD_SLOT_USED + 4) != sums[1])
                return false;
        }
        return get32(sav, crcAt() - 4) == FORMAT_WORD && get32(sav, crcAt()) == crc(sav);
    }

    // SaveMgr's slot checksum: the byte sum and the sum of the complements over 0xA8C bytes
    private static long[] sums(byte[] b, int o) {
        long sum = 0;
        for (int i = 0; i < HD_SLOT_USED; i++) sum += b[o + i] & 0xFF;
        long c0 = sum & 0xFFFFFFFFL;
        return new long[] {c0, (HD_SLOT_USED * 0xFFFFFFFFL - c0) & 0xFFFFFFFFL};
    }

    private static void putSums(byte[] b, int o) {
        long[] s = sums(b, o);
        put32(b, o + HD_SLOT_USED, s[0]);
        put32(b, o + HD_SLOT_USED + 4, s[1]);
    }

    private static int namesAt() {
        int p = 3 * HD_SLOT;
        for (int sect = 0; sect < NAME_SECTION; sect++) p += 3 * HD_EXTRA[sect];
        return p;
    }

    private static int crcAt() {
        int p = 3 * HD_SLOT;
        for (int size : HD_EXTRA) p += 3 * size;
        return p + 4;
    }

    // the CRC-32 of the HD-only sections; a name counts only (its characters + 1) bytes, zero padded to 18
    static long crc(byte[] b) {
        CRC32 crc = new CRC32();
        int p = 3 * HD_SLOT;
        for (int sect = 0; sect < HD_EXTRA.length; sect++) {
            for (int s = 0; s < 3; s++, p += HD_EXTRA[sect]) {
                byte[] part = Arrays.copyOfRange(b, p, p + HD_EXTRA[sect]);
                if (sect == NAME_SECTION) {
                    int chars = 0;
                    while (chars < 9 && (b[p + 2 * chars] != 0 || b[p + 2 * chars + 1] != 0)) chars++;
                    part = new byte[18];
                    System.arraycopy(b, p, part, 0, chars + 1);
                }
                crc.update(part);
            }
        }
        return crc.getValue();
    }

    private static long get32(byte[] b, int o) {
        return ((b[o] & 0xFFL) << 24) | ((b[o + 1] & 0xFFL) << 16) | ((b[o + 2] & 0xFFL) << 8) | (b[o + 3] & 0xFFL);
    }

    private static void put32(byte[] b, int o, long v) {
        b[o] = (byte) (v >>> 24);
        b[o + 1] = (byte) (v >>> 16);
        b[o + 2] = (byte) (v >>> 8);
        b[o + 3] = (byte) v;
    }
}
