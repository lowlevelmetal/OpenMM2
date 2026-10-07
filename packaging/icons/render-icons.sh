#!/bin/sh
# Regenerates the raster icons from openmm2.svg. Requires rsvg-convert
# (librsvg) and ImageMagick 7 (`magick`). The outputs are committed so that
# builds and CI do not need these tools.
set -eu
cd "$(dirname "$0")"
for size in 16 24 32 48 64 128 256 512; do
    rsvg-convert -w "$size" -h "$size" openmm2.svg -o "openmm2-$size.png"
done
# Windows icon with the sizes Explorer and the taskbar ask for. Entries are
# stored as PNG (supported since Windows Vista), which keeps the file small.
python3 - <<'PY'
import struct
sizes = [16, 24, 32, 48, 64, 128, 256]
images = [open(f"openmm2-{s}.png", "rb").read() for s in sizes]
out = struct.pack("<HHH", 0, 1, len(images))
offset = 6 + 16 * len(images)
for s, data in zip(sizes, images):
    out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
    offset += len(data)
open("openmm2.ico", "wb").write(out + b"".join(images))
PY
# NSIS wizard sidebar (164x314) and header (150x57) bitmaps.
magick -size 164x314 gradient:'#151d4a-#5a2d82' \
       \( openmm2-128.png \) -gravity center -geometry +0-40 -composite \
       -type TrueColor BMP3:installer-sidebar.bmp
magick -size 150x57 xc:'#ffffff' \( openmm2-48.png \) -gravity east -geometry +6+0 -composite \
       -type TrueColor BMP3:installer-header.bmp
