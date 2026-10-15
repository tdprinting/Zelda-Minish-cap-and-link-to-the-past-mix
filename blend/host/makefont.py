"""
Draws the co-op player's overlay font (blend/host/font.h) from DejaVu Sans Bold
(free license: https://dejavu-fonts.github.io/License.html). Each printable
ASCII character becomes an 8-bit coverage bitmap.

    python3 blend/host/makefont.py [path/to/DejaVuSans-Bold.ttf]
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

TTF = sys.argv[1] if len(sys.argv) > 1 else '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
SIZE = 12
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'font.h')


def main():
    font = ImageFont.truetype(TTF, SIZE)
    ascent, descent = font.getmetrics()
    h = ascent + descent
    glyphs = []
    for c in range(32, 127):
        ch = chr(c)
        adv = int(round(font.getlength(ch)))
        w = max(adv, 1) + 2
        im = Image.new('L', (w, h), 0)
        ImageDraw.Draw(im).text((1, 0), ch, font=font, fill=255)
        glyphs.append((adv, w, list(im.tobytes())))
    with open(OUT, 'w') as f:
        f.write('/* Made by blend/host/makefont.py from DejaVu Sans Bold %dpx (DejaVu fonts license). */\n' % SIZE)
        f.write('#define FONT_HEIGHT %d\n#define FONT_ASCENT %d\n' % (h, ascent))
        f.write('struct Glyph { unsigned char advance, width; unsigned short offset; };\n')
        data = []
        f.write('static const struct Glyph sGlyphs[95] = {\n')
        for adv, w, px in glyphs:
            f.write('    { %d, %d, %d },\n' % (adv, w, len(data)))
            data.extend(px)
        f.write('};\n')
        f.write('static const unsigned char sGlyphData[%d] = {\n' % len(data))
        for i in range(0, len(data), 24):
            f.write('    ' + ', '.join(str(v) for v in data[i:i + 24]) + ',\n')
        f.write('};\n')
    print('font.h: %d glyphs, %d px high, %d bytes' % (len(glyphs), h, len(data)))


if __name__ == '__main__':
    main()
