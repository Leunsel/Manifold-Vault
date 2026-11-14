#!/usr/bin/env python3
"""Generate Manifold-Vault/Resources/icon.ico in the Manifold look.

Draws a rounded tile with the Font Awesome vault glyph at 1024 px and
downsamples it with Lanczos: up to 64 px as uncompressed DIB, larger as PNG.
Requires Pillow.

Run:  python Tools/gen_icon.py
"""
import io
import os
import struct
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow is missing:  python -m pip install Pillow")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "Vendor", "fontawesome", "otfs", "fa-solid-900.otf")
ICO = os.path.join(ROOT, "Manifold-Vault", "Resources", "icon.ico")

SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
PNG_FROM = 128
BASE = 1024

VAULT = ""
GLYPH_SIZE = 580
EDGE = (116, 226, 186, 255)
TILE = (20, 22, 26, 255)
GLYPH = (46, 219, 158, 255)


def draw_base():
    image = Image.new("RGBA", (BASE, BASE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    margin = 40
    draw.rounded_rectangle((margin, margin, BASE - margin, BASE - margin), radius=190, fill=EDGE)
    edge = 44
    draw.rounded_rectangle((margin + edge, margin + edge, BASE - margin - edge, BASE - margin - edge),
                           radius=190 - edge, fill=TILE)

    font = ImageFont.truetype(FONT, GLYPH_SIZE)
    left, top, right, bottom = draw.textbbox((0, 0), VAULT, font=font)
    draw.text(((BASE - (right - left)) / 2 - left, (BASE - (bottom - top)) / 2 - top), VAULT,
              font=font, fill=GLYPH)
    return image


def dib_entry(image):
    width, height = image.size
    pixels = image.load()
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    colour = bytearray()
    for y in range(height - 1, -1, -1):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            colour += bytes((b, g, r, a))
    return header + bytes(colour) + bytes(((width + 31) // 32) * 4 * height)


def png_entry(image):
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=True)
    return buffer.getvalue()


def main():
    if not os.path.exists(FONT):
        sys.exit("Font not found: %s" % FONT)
    base = draw_base()
    entries = []
    for size in SIZES:
        scaled = base.resize((size, size), Image.Resampling.LANCZOS)
        entries.append((size, png_entry(scaled) if size >= PNG_FROM else dib_entry(scaled)))

    out = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for size, data in entries:
        out += struct.pack("<BBBBHHII", 0 if size == 256 else size, 0 if size == 256 else size,
                           0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in entries:
        out += data
    with open(ICO, "wb") as file:
        file.write(out)
    print("%s: %d images, %d bytes" % (os.path.relpath(ICO, ROOT), len(entries), len(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
