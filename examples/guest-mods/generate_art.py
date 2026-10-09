#!/usr/bin/env python3
"""Generate original geometric HUD art. No fonts, game data or external images.

The PNG encoder uses stored DEFLATE blocks so output is identical across zlib versions.
"""
import argparse
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parent
GLYPHS = {
    'A': ['01110','11011','11011','11111','11011','11011','11011'],
    'B': ['11110','11011','11011','11110','11011','11011','11110'],
    'X': ['11011','11011','01110','00100','01110','11011','11011'],
    'Y': ['11011','11011','01110','00100','00100','00100','00100'],
    'R': ['11110','11011','11011','11110','11100','11010','11011'],
}
COLORS = {'A': (46,177,83), 'B': (226,62,58), 'X': (45,125,224),
          'Y': (239,193,52), 'R': (155,168,184)}


def png(width, height, pixels):
    raw = b''.join(b'\0' + pixels[y*width*4:(y+1)*width*4] for y in range(height))
    assert len(raw) <= 65535
    compressed = b'\x78\x01\x01' + struct.pack('<HH', len(raw), 65535-len(raw)) + raw
    compressed += struct.pack('>I', zlib.adler32(raw))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width,height,8,6,0,0,0)) + chunk(b'IDAT', compressed) + chunk(b'IEND', b'')


def icon(letter):
    data = bytearray()
    height = 40 if letter == 'R' else 64
    for y in range(height):
        for x in range(64):
            if letter == 'R':
                dx,dy=max(5-x,x-58,0),max(5-y,y-34,0)
                if dx*dx+dy*dy>25:
                    data.extend((0,0,0,0)); continue
                color=(16,21,29) if x<3 or x>60 or y<3 or y>36 else COLORS[letter]
                gx,gy=(x-19)//5,(y-2)//5
                if 0<=gx<5 and 0<=gy<7 and GLYPHS[letter][gy][gx]=='1':color=(20,25,31)
                data.extend((*color,255));continue
            distance = (x-31.5)**2 + (y-31.5)**2
            if distance > 31**2:
                data.extend((0,0,0,0)); continue
            color = (16,21,29) if distance > 28**2 else COLORS[letter]
            gx, gy = (x-19)//5, (y-14)//5
            if 0 <= gx < 5 and 0 <= gy < 7 and GLYPHS[letter][gy][gx] == '1':
                color = (20,25,31)
            data.extend((*color,255))
    return png(64,height,data)


def tile():
    data = bytearray()
    for y in range(32):
        for x in range(32):
            if abs(x-15.5) + abs(y-15.5) < 12:
                color = (242,178,63,255)
            else:
                color = (38,98,153,255)
            data.extend(color)
    return png(32,32,data)


def artifacts():
    yield ROOT/'hud-demo/assets/tile.png', tile()
    for letter in GLYPHS:
        yield ROOT/f'button-icons/assets/{letter.lower()}.png', icon(letter)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    args = parser.parse_args()
    for path, data in artifacts():
        if args.check:
            if not path.is_file() or path.read_bytes() != data:
                raise SystemExit(f'Original example art differs: {path.name}')
        else:
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(data)


if __name__ == '__main__':
    main()
