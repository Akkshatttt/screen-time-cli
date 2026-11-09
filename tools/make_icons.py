"""Builds every icon file from the one source PNG. Re-run after changing the artwork:

    python tools/make_icons.py [--preview PATH]

Writes assets/icons/ico/screen-time-cli.ico (all sizes, embedded in the exes), one single-size .ico per size in
assets/icons/ico/by-size/, and PNGs in assets/icons/png/.

The glyph is only ~42% of the source canvas, so a plain downscale would be unreadable at 16-32 px. Small sizes use a
tighter centre crop (glyph fills more of the frame); large sizes show the full artwork.
"""
import argparse
import io
import os
import struct
import sys
from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets", "icons", "source", "terminal-hourglass-1254.png")
OUT = os.path.join(ROOT, "assets", "icons")
NAME = "screen-time-cli"

# size -> fraction of the frame the glyph (bright part) should fill
SIZES = {16: 0.80, 20: 0.78, 24: 0.76, 32: 0.72, 40: 0.68, 48: 0.64, 64: 0.60, 96: 0.52, 128: 0.48, 256: 0.42}
GLYPH_BOX = (362, 415, 891, 859)   # bright glyph bounding box in the 1254 px source (measured)
CORNER = 0.215                      # corner radius as a fraction of the icon size
FULL_INSET = 0.015                  # trim the source's opaque black corner pixels when showing the full artwork


def rounded_mask(size):
    """Anti-aliased rounded-square alpha mask (drawn at 8x then reduced)."""
    big = size * 8
    m = Image.new("L", (big, big), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, big - 1, big - 1), radius=int(big * CORNER), fill=255)
    return m.resize((size, size), Image.LANCZOS)


def crop_for(src, size, frac):
    w, h = src.size
    cx = (GLYPH_BOX[0] + GLYPH_BOX[2]) / 2
    cy = (GLYPH_BOX[1] + GLYPH_BOX[3]) / 2
    side = (GLYPH_BOX[2] - GLYPH_BOX[0]) / frac
    full = w * (1 - 2 * FULL_INSET)
    if side >= full:                                   # full artwork, minus the black corner fringe
        side = full
        cx = cy = w / 2
    x0 = min(max(cx - side / 2, 0), w - side)
    y0 = min(max(cy - side / 2, 0), h - side)
    return src.crop((round(x0), round(y0), round(x0 + side), round(y0 + side)))


def make(src, size, frac):
    im = crop_for(src, size, frac).resize((size, size), Image.LANCZOS)
    if size <= 48:                                     # small sizes lose edge contrast when reduced
        im = im.filter(ImageFilter.UnsharpMask(radius=0.7, percent=70, threshold=2))
    im = im.convert("RGBA")
    im.putalpha(ImageChops.multiply(im.getchannel("A"), rounded_mask(size)))
    return im


def write_ico(path, frames):
    """ICO container with PNG-compressed frames (Windows Vista and later). Pillow's ICO writer resamples one image
    instead of storing the per-size frames made above, so the container is written by hand: it is only a header,
    one 16-byte directory entry per frame, then the PNG data."""
    blobs = []
    for im in frames:
        buf = io.BytesIO()
        im.save(buf, format="PNG", optimize=True)
        blobs.append(buf.getvalue())
    out = [struct.pack("<HHH", 0, 1, len(frames))]
    offset = 6 + 16 * len(frames)
    for im, blob in zip(frames, blobs):
        w = 0 if im.width >= 256 else im.width       # 0 means 256 in an ICO directory
        h = 0 if im.height >= 256 else im.height
        out.append(struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, len(blob), offset))
        offset += len(blob)
    with open(path, "wb") as f:
        f.write(b"".join(out))
        for blob in blobs:
            f.write(blob)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="also write a contact sheet PNG to this path")
    args = ap.parse_args()
    if not os.path.isfile(SRC):
        sys.exit("missing source image: " + SRC)
    src = Image.open(SRC).convert("RGB")
    for d in ("ico", os.path.join("ico", "by-size"), "png"):
        os.makedirs(os.path.join(OUT, d), exist_ok=True)

    frames = {s: make(src, s, f) for s, f in SIZES.items()}
    ordered = [frames[s] for s in sorted(frames)]
    write_ico(os.path.join(OUT, "ico", NAME + ".ico"), ordered)
    for s, im in frames.items():
        write_ico(os.path.join(OUT, "ico", "by-size", "%s-%d.ico" % (NAME, s)), [im])
        im.save(os.path.join(OUT, "png", "%s-%d.png" % (NAME, s)), optimize=True)
    print("wrote", len(frames), "sizes:", ", ".join(str(s) for s in sorted(frames)))

    if args.preview:                                   # light and dark backgrounds, small sizes enlarged 4x
        sizes = sorted(frames)
        pad = 12
        row_h = 4 * 40 + pad
        sheet = Image.new("RGBA", (1100, row_h * 2 + pad), (0, 0, 0, 0))
        for r, bg in enumerate(((245, 245, 245, 255), (32, 32, 32, 255))):
            ImageDraw.Draw(sheet).rectangle((0, r * row_h, 1100, (r + 1) * row_h), fill=bg)
            x = pad
            for s in sizes:
                zoom = 4 if s <= 40 else 1
                im = frames[s].resize((s * zoom, s * zoom), Image.NEAREST) if zoom > 1 else frames[s]
                if im.width > 150:
                    im = im.resize((150, 150), Image.LANCZOS)
                sheet.alpha_composite(im, (x, r * row_h + pad))
                x += im.width + pad
        sheet.save(args.preview)
        print("preview:", args.preview)


if __name__ == "__main__":
    main()
