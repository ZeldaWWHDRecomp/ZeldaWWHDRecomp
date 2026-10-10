import org.wwhdrecomp.wwhd.TgaImage;

public final class TgaImageTest {
    private static void expect(boolean ok, String what) {
        if (!ok) throw new AssertionError(what);
    }
    // a 2x2 image: bottom row first (descriptor bit 5 clear), BGR(A) bytes
    private static byte[] image(int bits, int descriptor, int[][] bgra) {
        int bytes = bits / 8;
        byte[] data = new byte[18 + 4 * bytes];
        data[2] = 2; data[12] = 2; data[14] = 2; data[16] = (byte) bits; data[17] = (byte) descriptor;
        for (int i = 0; i < 4; i++)
            for (int c = 0; c < bytes; c++) data[18 + i * bytes + c] = (byte) bgra[i][c];
        return data;
    }
    private static void rejects(byte[] data, String what) {
        try { TgaImage.decode(data); }
        catch (java.io.IOException expected) { return; }
        throw new AssertionError("accepted " + what);
    }
    public static void main(String[] args) throws Exception {
        int[][] px = {{1, 2, 3, 255}, {4, 5, 6, 128}, {7, 8, 9, 255}, {10, 11, 12, 0}};
        TgaImage bottomFirst = TgaImage.decode(image(32, 8, px));
        expect(bottomFirst.width == 2 && bottomFirst.height == 2, "size");
        // the file's first row is the picture's bottom row
        expect(bottomFirst.argb[2] == (255 << 24 | 3 << 16 | 2 << 8 | 1), "bottom-left pixel");
        expect(bottomFirst.argb[3] == (128 << 24 | 6 << 16 | 5 << 8 | 4), "alpha kept");
        expect(bottomFirst.argb[0] == (255 << 24 | 9 << 16 | 8 << 8 | 7), "top-left pixel");
        TgaImage topFirst = TgaImage.decode(image(32, 0x28, px));
        expect(topFirst.argb[0] == bottomFirst.argb[2], "top-first rows");
        TgaImage rgb = TgaImage.decode(image(24, 0, px));
        expect(rgb.argb[1] >>> 24 == 255, "24 bits are opaque");
        byte[] rle = image(32, 8, px); rle[2] = 10;
        rejects(rle, "RLE");
        rejects(java.util.Arrays.copyOf(image(32, 8, px), 30), "truncated pixels");
        rejects(new byte[10], "short header");
        System.out.println("TgaImageTest passed");
    }
}
