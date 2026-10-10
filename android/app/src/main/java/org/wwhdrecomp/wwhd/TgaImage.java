package org.wwhdrecomp.wwhd;

import java.io.IOException;

/** An uncompressed true-colour TGA (type 2, 24 or 32 bits), such as the game's meta/iconTex.tga.
 *  Plain Java (no Android classes), so tools/android/TgaImageTest.java runs it on the host. */
public final class TgaImage {
    public final int width, height;
    public final int[] argb;  // top row first

    private TgaImage(int width, int height, int[] argb) {
        this.width = width;
        this.height = height;
        this.argb = argb;
    }

    public static TgaImage decode(byte[] data) throws IOException {
        if (data.length < 18) throw new IOException("TGA header missing");
        int idLength = data[0] & 255, colourMapType = data[1] & 255, type = data[2] & 255;
        int width = (data[12] & 255) | (data[13] & 255) << 8, height = (data[14] & 255) | (data[15] & 255) << 8;
        int bits = data[16] & 255, descriptor = data[17] & 255;
        if (colourMapType != 0 || type != 2 || (bits != 24 && bits != 32) || width <= 0 || height <= 0 ||
                width > 1024 || height > 1024)
            throw new IOException("Unsupported TGA (type " + type + ", " + bits + " bits)");
        int bytes = bits / 8, start = 18 + idLength;
        if (data.length < start + width * height * bytes) throw new IOException("TGA pixel data truncated");
        boolean topFirst = (descriptor & 0x20) != 0;
        int[] argb = new int[width * height];
        for (int row = 0; row < height; row++) {
            int target = (topFirst ? row : height - 1 - row) * width;
            for (int x = 0; x < width; x++) {
                int at = start + (row * width + x) * bytes;
                int b = data[at] & 255, g = data[at + 1] & 255, r = data[at + 2] & 255;
                int a = bytes == 4 ? data[at + 3] & 255 : 255;
                argb[target + x] = a << 24 | r << 16 | g << 8 | b;
            }
        }
        return new TgaImage(width, height, argb);
    }
}
