package org.wwhdrecomp.wwhd;

import java.nio.charset.StandardCharsets;

// SaveScrub (Share log with the save files): run with java -ea. A synthetic cking.sav with names in slots 0 and 2
// and an empty slot 1: the copy is valid, says "Link" wherever a name was, changes nothing else but the name bytes
// and the checks, and anything that is not a valid cking.sav is left out (null).
public final class SaveScrubTest {
    static final int INFO = 0x143;

    static void put32(byte[] b, int o, long v) {
        b[o] = (byte) (v >>> 24); b[o + 1] = (byte) (v >>> 16); b[o + 2] = (byte) (v >>> 8); b[o + 3] = (byte) v;
    }

    static byte[] fixture() {
        byte[] b = new byte[SaveScrub.HD_FILE];
        String[] names = {"Abahbob", null, "Tetra16chars"};
        for (int s = 0; s < 3; s++) {
            int o = s * SaveScrub.HD_SLOT;
            for (int i = 0; i < 0x768; i++) b[o + i] = (byte) (i * 7 + s);  // some save data
            for (int f : new int[] {0x14, 0x25, 0x36}) java.util.Arrays.fill(b, o + INFO + f, o + INFO + f + 17, (byte) 0);
            if (names[s] != null) {
                byte[] n = names[s].getBytes(StandardCharsets.ISO_8859_1);
                System.arraycopy(n, 0, b, o + INFO + 0x14, Math.min(16, n.length));
                System.arraycopy("Fishman".getBytes(StandardCharsets.ISO_8859_1), 0, b, o + INFO + 0x25, 7);
            }
            long sum = 0;
            for (int i = 0; i < SaveScrub.HD_SLOT_USED; i++) sum += b[o + i] & 0xFF;
            put32(b, o + SaveScrub.HD_SLOT_USED, sum & 0xFFFFFFFFL);
            put32(b, o + SaveScrub.HD_SLOT_USED + 4, (SaveScrub.HD_SLOT_USED * 0xFFFFFFFFL - (sum & 0xFFFFFFFFL)) & 0xFFFFFFFFL);
        }
        int p = 3 * SaveScrub.HD_SLOT;
        for (int i = p; i < SaveScrub.HD_FILE - 8; i++) b[i] = (byte) (i * 13);  // player, status, event sections
        int names16 = p + 3 * (16 + 4 + 20);
        for (int s = 0; s < 3; s++) {
            int q = names16 + 18 * s;
            java.util.Arrays.fill(b, q, q + 18, (byte) 0);
            if (names[s] != null) {
                byte[] n = names[s].substring(0, Math.min(8, names[s].length())).getBytes(StandardCharsets.UTF_16BE);
                System.arraycopy(n, 0, b, q, n.length);
            }
        }
        put32(b, SaveScrub.HD_FILE - 8, SaveScrub.FORMAT_WORD);
        put32(b, SaveScrub.HD_FILE - 4, SaveScrub.crc(b));
        return b;
    }

    public static void main(String[] args) {
        boolean ea = false;
        assert ea = true;
        if (!ea) throw new AssertionError("run with java -ea");
        byte[] sav = fixture();
        assert SaveScrub.valid(sav);
        byte[] out = SaveScrub.withoutNames(sav);
        assert out != null && SaveScrub.valid(out);
        int names16 = 3 * SaveScrub.HD_SLOT + 3 * (16 + 4 + 20);
        for (int s = 0; s < 3; s++) {
            int o = s * SaveScrub.HD_SLOT;
            String n = new String(out, o + INFO + 0x14, 17, StandardCharsets.ISO_8859_1).replace("\0", "");
            String other = new String(out, o + INFO + 0x25, 17, StandardCharsets.ISO_8859_1).replace("\0", "");
            String hd = new String(out, names16 + 18 * s, 18, StandardCharsets.UTF_16BE).replace("\0", "");
            assert n.equals(s == 1 ? "" : "Link") : n;
            assert other.equals(s == 1 ? "" : "Link") : other;
            assert hd.equals(s == 1 ? "" : "Link") : hd;
        }
        // only names, sums and the CRC changed
        for (int i = 0; i < sav.length; i++) {
            if (sav[i] == out[i]) continue;
            int s = i / SaveScrub.HD_SLOT, o = i % SaveScrub.HD_SLOT;
            boolean name = i < 3 * SaveScrub.HD_SLOT && ((o >= INFO + 0x14 && o < INFO + 0x14 + 17) || (o >= INFO + 0x25 && o < INFO + 0x25 + 17));
            boolean sums = i < 3 * SaveScrub.HD_SLOT && o >= SaveScrub.HD_SLOT_USED;
            boolean hdName = i >= names16 && i < names16 + 54;
            boolean crc = i >= SaveScrub.HD_FILE - 4;
            assert name || sums || hdName || crc : "byte " + i + " (slot " + s + ")";
        }
        // the original isn't changed
        assert java.util.Arrays.equals(sav, fixture());
        // not a valid cking.sav: left out
        assert SaveScrub.withoutNames(new byte[100]) == null;
        assert SaveScrub.withoutNames(null) == null;
        byte[] bad = sav.clone();
        bad[10] ^= 1;  // a slot's data changed without its sums
        assert SaveScrub.withoutNames(bad) == null;
        bad = sav.clone();
        bad[names16 + 40] ^= 1;  // the HD data changed without the CRC
        assert SaveScrub.withoutNames(bad) == null;
        bad = sav.clone();
        put32(bad, SaveScrub.HD_FILE - 8, 3);  // another format
        assert SaveScrub.withoutNames(bad) == null;
        // a save with the port's story block (the whole name of a long name, story records): the block is left out
        byte[] block = new byte[3 * 0x400 + 16];
        java.util.Arrays.fill(block, 0, 3 * 0x400, (byte) 0x4C);
        block[3 * 0x400] = 'W'; block[3 * 0x400 + 1] = 'S'; block[3 * 0x400 + 2] = 'T'; block[3 * 0x400 + 3] = 'B';
        put32(block, 3 * 0x400 + 4, 3 * 0x400);
        byte[] withBlock = java.util.Arrays.copyOf(sav, sav.length + block.length);
        System.arraycopy(block, 0, withBlock, sav.length, block.length);
        byte[] fromBlock = SaveScrub.withoutNames(withBlock);
        assert fromBlock != null && java.util.Arrays.equals(fromBlock, out);
        withBlock[withBlock.length - 16] = 'X';  // no footer: not a cking.sav
        assert SaveScrub.withoutNames(withBlock) == null;
        // a scrubbed copy scrubs to itself
        assert java.util.Arrays.equals(SaveScrub.withoutNames(out), out);
        System.out.println("SaveScrubTest: ok");
    }
}
