#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow>=10"]
# ///
"""Render poem text files to 296x128 1-bit bitmaps for the e-paper panel.

    uv run tools/render_poems.py               # poems_src/ -> out/
    python3 tools/render_poems.py SRC -o OUT   # needs: pip install pillow

Input:  SRC/*.txt, processed in filename order. Each file is
            key: value          header lines; `author` required, `title` optional
            <blank line>
            poem body           blank lines separate stanzas; leading spaces
                                are kept as indentation
Output: OUT/poems/NNN.bin       copy this folder to the SD card root
        OUT/previews/NNN.png    decoded back from the .bin, scaled 3x
        OUT/previews/contact.png

Bitmap format (HARDWARE.md §4.6): 37 bytes/row x 128 rows = 4736 bytes,
row-major, MSB = leftmost pixel, 1 = black.
"""

import argparse
import functools
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

W, H = 296, 128
BIN_SIZE = (W // 8) * H  # 4736

# --- Typography knobs -------------------------------------------------------
# Georgia was drawn for low-resolution screens and is hand-hinted for exactly
# this case: 1-bit rendering at 10-16 px. Most book faces (Charter, Palatino,
# Baskerville) are not, and break up without antialiasing. Times New Roman
# was tried as a narrower fallback and rejected on legibility.
# These are macOS system font paths; point them elsewhere on another OS.
_FONTS = "/System/Library/Fonts/Supplemental/"
BODY_FONT = _FONTS + "Georgia.ttf"
ATTR_FONT = _FONTS + "Georgia Italic.ttf"

MAX_SIZE = 16      # px; cap so a six-line poem does not become a billboard
COMFORT_SIZE = 11  # px; below this we would rather wrap a line than shrink
MIN_SIZE = 10      # px; VERIFY on hardware. 9 px Georgia spaces unevenly.
ATTR_SIZE = 10     # px; attribution line, bottom right

LEADING = 1.2   # baseline-to-baseline distance, in multiples of font size
HANG_EM = 1.5   # indent of the turned-over part of a wrapped line
MARGIN = 5      # px kept clear on every edge (bezel tolerance)
GUTTER = 14     # px between columns in two-column layouts
# Top-right corner the firmware draws the battery glyph into at refresh time
# (HARDWARE.md §7): 16x8 px, as (x0, y0, x1, y1). The bitmap leaves it blank.
# It sits here rather than beside the attribution because the bottom row is
# where the space is tight; a first line rarely reaches this far right.
BATTERY_GLYPH = (W - MARGIN - 16, MARGIN, W - MARGIN, MARGIN + 8)

# Preview colours: e-paper is dark grey on light grey, not black on white.
PAPER, INK = (228, 228, 222), (38, 38, 38)
PLACEHOLDER = (150, 150, 146)  # preview-only marker for the battery glyph


def candidates():
    """(size, columns, wrap) in order of preference; the first one that fits
    wins. This generator is the whole fitting policy:
      1. Largest size first down to COMFORT_SIZE, every line kept whole.
      2. Otherwise largest size first down to MIN_SIZE; at each size, lines
         kept whole if possible, wrapped (one column only) if not.
    At equal size, one column is preferred to two. Two-column layouts never
    wrap: a narrow column where most lines turn over is worse than a smaller
    size, and worse than leaving the poem out."""
    for size in range(MAX_SIZE, COMFORT_SIZE - 1, -1):
        for cols in (1, 2):
            yield size, cols, False
    for size in range(MAX_SIZE, MIN_SIZE - 1, -1):
        yield size, 1, False
        yield size, 2, False
        yield size, 1, True


@dataclass
class Poem:
    name: str
    title: str
    author: str
    stanzas: list  # list of stanzas; a stanza is a list of line strings


@dataclass
class Face:
    """A font at one pixel size, plus the vertical metrics layout needs."""
    size: int
    font: ImageFont.FreeTypeFont
    asc: int    # baseline up to the top of the tallest ascender
    desc: int   # baseline down to the bottom of the deepest descender
    pitch: int  # baseline-to-baseline distance
    gap: int    # extra space between stanzas (half a line, to save height)

    def width(self, text):
        # mode="1" matters. Pillow measures with antialiased metrics unless
        # told otherwise, but 1-bit rendering snaps every glyph advance to
        # whole pixels and comes out up to 20% WIDER at these sizes (Georgia
        # 11px: 162 px measured vs 191 px drawn for the same line).
        return math.ceil(self.font.getlength(text, mode="1"))


@functools.lru_cache(maxsize=None)
def load_face(path, size):
    # Layout.BASIC: plain FreeType advances. Avoids output differing between
    # machines depending on whether Pillow was built with libraqm.
    font = ImageFont.truetype(path, size, layout_engine=ImageFont.Layout.BASIC)
    # Measured from real glyphs rather than the font's declared ascent, which
    # includes headroom for accents and would waste a pixel or two per poem.
    asc = -font.getbbox("bdfhklT(", mode="1", anchor="ls")[1]
    desc = font.getbbox("gjpqy,(", mode="1", anchor="ls")[3]
    pitch = round(size * LEADING)
    return Face(size, font, asc, desc, pitch, pitch // 2)


def load_poem(path):
    head, _, body = path.read_text(encoding="utf-8").partition("\n\n")
    meta = {}
    for line in head.splitlines():
        key, sep, value = line.partition(":")
        if not sep:
            sys.exit(f"{path}: expected 'key: value' in header, got {line!r}")
        meta[key.strip().lower()] = value.strip()
    if not meta.get("author") or not body.strip():
        sys.exit(f"{path}: needs an 'author:' header, a blank line, then the poem")
    stanzas = [
        [line.rstrip().expandtabs(4) for line in block.splitlines()]
        for block in re.split(r"\n[ \t]*\n\s*", body.strip("\n"))
    ]
    return Poem(path.stem, meta.get("title", ""), meta["author"], stanzas)


# --- Layout -----------------------------------------------------------------

def break_line(line, face, wrap_w):
    """One poem line -> [(dx, text)] rows. More than one row only if the line
    is wider than wrap_w; continuation rows get a hanging indent, the usual
    convention for showing 'this is still the same line of verse'."""
    text = line.lstrip(" ")
    indent = face.width(line[: len(line) - len(text)])
    if wrap_w is None or indent + face.width(text) <= wrap_w:
        return [(indent, text)]
    rows, current, dx = [], "", indent
    for word in text.split(" "):
        trial = f"{current} {word}" if current else word
        if current and dx + face.width(trial) > wrap_w:
            rows.append((dx, current))
            current, dx = word, indent + round(HANG_EM * face.size)
        else:
            current = trial
    rows.append((dx, current))
    return rows


@dataclass
class Column:
    rows: list  # (dx, dy, text); dy = baseline offset from the first baseline
    w: int
    h: int


def set_column(stanzas, face, wrap_w):
    rows, dy = [], 0
    for i, stanza in enumerate(stanzas):
        if i:
            dy += face.gap
        for line in stanza:
            for dx, text in break_line(line, face, wrap_w):
                rows.append((dx, dy, text))
                dy += face.pitch
    w = max(dx + face.width(text) for dx, _, text in rows)
    h = face.asc + rows[-1][1] + face.desc
    return Column(rows, w, h)


def two_column_options(stanzas, face, wrap_w):
    """Every way to split the poem into two columns, best first. Cuts fall
    only between stanzas; a single-stanza poem may be cut between any lines."""
    if len(stanzas) > 1:
        cuts = [(stanzas[:i], stanzas[i:]) for i in range(1, len(stanzas))]
    else:
        lines = stanzas[0]
        cuts = [([lines[:i]], [lines[i:]]) for i in range(1, len(lines))]
    pairs = [[set_column(a, face, wrap_w), set_column(b, face, wrap_w)] for a, b in cuts]
    # Prefer the taller column on the left (the attribution then sits under
    # the short one), and among those the most compact split.
    return sorted(pairs, key=lambda p: (p[0].h < p[1].h, max(p[0].h, p[1].h)))


def overlaps(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def try_layout(poem, face, cols, wrap, attr_box):
    """Place the poem body at one size/column-count. Returns [(x, baseline_y,
    text)] or None if it does not fit, or would touch the attribution or the
    battery glyph."""
    gx0, _, _, gy1 = BATTERY_GLYPH
    keepouts = [attr_box, (gx0 - 5, 0, W, gy1 + 3)]
    avail_w = W - 2 * MARGIN
    col_max = avail_w if cols == 1 else (avail_w - GUTTER) // 2
    wrap_w = col_max if wrap else None

    if cols == 1:
        options = [[set_column(poem.stanzas, face, wrap_w)]]
    else:
        options = two_column_options(poem.stanzas, face, wrap_w)

    for columns in options:
        total_w = sum(c.w for c in columns) + GUTTER * (cols - 1)
        height = max(c.h for c in columns)
        if total_w > avail_w:
            continue
        # Lines are always left-aligned within the block. Three placements
        # for the block itself, in order of preference:
        #   1. centred on the whole panel (the last line may share its row
        #      with the attribution if it is short enough to stay clear)
        #   2. same height, pushed to the left margin to get clear
        #   3. centred in the space above the attribution
        centred = (W - total_w) // 2
        for left, bottom in ((centred, H - MARGIN), (MARGIN, H - MARGIN),
                             (centred, attr_box[1])):
            if height > bottom - MARGIN:
                continue
            first_baseline = MARGIN + (bottom - MARGIN - height) // 2 + face.asc
            placed, x = [], left
            for col in columns:
                for dx, dy, text in col.rows:
                    placed.append((x + dx, first_baseline + dy, text))
                x += col.w + GUTTER
            boxes = [(px, py - face.asc, px + face.width(text), py + face.desc)
                     for px, py, text in placed]
            if not any(overlaps(box, k) for box in boxes for k in keepouts):
                return placed
    return None


def render(poem):
    """Returns (bitmap_bytes, description) or (None, reason)."""
    attr = f"{poem.title} — {poem.author}" if poem.title else f"— {poem.author}"
    attr_face = load_face(ATTR_FONT, ATTR_SIZE)
    attr_x = W - MARGIN - attr_face.width(attr)
    attr_y = H - MARGIN - attr_face.desc  # baseline
    if attr_x < MARGIN:
        return None, f"attribution too wide: {attr!r}"
    # No body row may touch this box. Everything outside it is fair game, so
    # a poem whose last line is short gets the bottom row for free.
    attr_box = (attr_x - 10, attr_y - attr_face.asc - 3, W, H)

    for size, cols, wrap in candidates():
        face = load_face(BODY_FONT, size)
        placed = try_layout(poem, face, cols, wrap, attr_box)
        if placed:
            break
    else:
        return None, f"does not fit, even at {MIN_SIZE}px"

    # Mode "1" with ink drawn as 1 on a 0 background: tobytes() is then the
    # device format directly (8 px/byte, MSB first, rows unpadded as 296 % 8 == 0).
    img = Image.new("1", (W, H), 0)
    draw = ImageDraw.Draw(img)  # mode "1" => FreeType mono rendering, no AA
    for x, y, text in placed:
        draw.text((x, y), text, font=face.font, fill=1, anchor="ls")
    draw.text((attr_x, attr_y), attr, font=attr_face.font, fill=1, anchor="ls")
    data = img.tobytes()
    assert len(data) == BIN_SIZE

    n_lines = sum(len(s) for s in poem.stanzas)
    desc = f"{size}px, {cols} col"
    if len(placed) > n_lines:
        desc += f", {len(placed) - n_lines} wrapped"
    return data, desc


# --- Preview ----------------------------------------------------------------

def decode_bin(data, scale=3):
    """Unpack a .bin exactly as HARDWARE.md describes the format, without
    going through Pillow's own bit-packing, so the preview is an independent
    check of the bytes the firmware will read. The grey battery outline is
    drawn on the preview only, to show where the firmware's glyph will go."""
    assert len(data) == BIN_SIZE
    img = Image.new("RGB", (W, H), PAPER)
    x0, y0, x1, y1 = BATTERY_GLYPH
    draw = ImageDraw.Draw(img)
    draw.rectangle((x0, y0, x1 - 3, y1 - 1), outline=PLACEHOLDER)
    draw.rectangle((x1 - 2, y0 + 2, x1 - 1, y1 - 3), fill=PLACEHOLDER)
    px = img.load()
    for y in range(H):
        for x in range(W):
            if data[y * (W // 8) + x // 8] & (0x80 >> (x % 8)):
                px[x, y] = INK
    return img.resize((W * scale, H * scale), Image.NEAREST)


def contact_sheet(entries, path, scale=2):
    """entries: [(label, bin_bytes)] -> one tall PNG with captions."""
    label_font = ImageFont.truetype(BODY_FONT, 13)
    pad, cap_h = 16, 22
    cell_h = H * scale + cap_h + pad
    sheet = Image.new("RGB", (W * scale + 2 * pad, pad + cell_h * len(entries)), (128, 128, 128))
    draw = ImageDraw.Draw(sheet)
    for i, (label, data) in enumerate(entries):
        y = pad + i * cell_h
        sheet.paste(decode_bin(data, scale), (pad, y))
        draw.text((pad, y + H * scale + 4), label, font=label_font, fill=(255, 255, 255))
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("src", nargs="?", default="poems_src", type=Path)
    ap.add_argument("-o", "--out", default="out", type=Path)
    args = ap.parse_args()

    sources = sorted(args.src.glob("*.txt"))
    if not sources:
        sys.exit(f"no .txt files in {args.src}/")

    rendered, failed = [], []
    for path in sources:
        poem = load_poem(path)
        data, info = render(poem)
        if data is None:
            failed.append((poem.name, info))
        else:
            rendered.append((poem.name, info, data))

    # Clear previous numbered output first: the firmware counts the files in
    # poems/, so a stale 006.bin from an earlier, longer run would be shown.
    bin_dir, png_dir = args.out / "poems", args.out / "previews"
    for d, ext in ((bin_dir, "bin"), (png_dir, "png")):
        d.mkdir(parents=True, exist_ok=True)
        for old in d.glob(f"[0-9][0-9][0-9].{ext}"):
            old.unlink()

    sheet = []
    for n, (name, info, data) in enumerate(rendered, start=1):
        (bin_dir / f"{n:03d}.bin").write_bytes(data)
        decode_bin(data).save(png_dir / f"{n:03d}.png")
        sheet.append((f"{n:03d}  {name}  ({info})", data))
        print(f"{n:03d}.bin  {name:28s} {info}")
    if sheet:
        contact_sheet(sheet, png_dir / "contact.png")
        print(f"\n{len(sheet)} poems -> {bin_dir}/   previews -> {png_dir}/contact.png")

    for name, reason in failed:
        print(f"SKIPPED  {name}: {reason}", file=sys.stderr)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
