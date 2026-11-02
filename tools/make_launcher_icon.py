"""Draw the launcher's icon: launcher/res/split2.png and split2.ico.

    py -3 tools/make_launcher_icon.py

Our own drawing (no artwork from the game): a stopwatch split along a
diagonal, its two halves pulled apart, amber and teal, on a dark disc.
Drawn at 1024 px and scaled down for each size the .ico carries.
"""
import math
import os

from PIL import Image, ImageChops, ImageDraw

S = 1024
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "launcher", "res")

AMBER, TEAL = (255, 167, 38, 255), (38, 198, 218, 255)
DARK, RIM, HAND = (22, 27, 38, 255), (236, 240, 245, 255), (22, 27, 38, 255)


def watch(face):
    """One whole stopwatch with the given face colour."""
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx, cy, r = S // 2, S // 2 + 50, 370
    d.rounded_rectangle((cx - 60, cy - r - 120, cx + 60, cy - r - 40), 24, fill=RIM)   # crown
    d.rectangle((cx - 28, cy - r - 50, cx + 28, cy - r + 10), fill=RIM)
    d.ellipse((cx - r - 34, cy - r - 34, cx + r + 34, cy + r + 34), fill=RIM)            # rim
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=face)
    for k in range(12):                                                                    # ticks
        a = k * math.pi / 6
        r0 = r - (70 if k % 3 == 0 else 45)
        d.line((cx + r0 * math.sin(a), cy - r0 * math.cos(a),
                cx + (r - 18) * math.sin(a), cy - (r - 18) * math.cos(a)),
               fill=HAND, width=26 if k % 3 == 0 else 14)
    d.line((cx, cy, cx + 200 * math.sin(1.0), cy - 200 * math.cos(1.0)), fill=HAND, width=34)
    d.line((cx, cy, cx + 280 * math.sin(4.0), cy - 280 * math.cos(4.0)), fill=HAND, width=22)
    d.ellipse((cx - 36, cy - 36, cx + 36, cy + 36), fill=HAND)
    return im


def half(mask_upper):
    """A mask for one side of the jagged split from top right to bottom left."""
    m = Image.new("L", (S, S), 0)
    pts = [(S * 0.80, -10), (S * 0.56, S * 0.40), (S * 0.66, S * 0.46), (S * 0.40, S * 0.86),
           (S * 0.20, S + 10)]
    poly = ([(-10, -10)] + pts + [(-10, S + 10)]) if mask_upper else ([(S + 10, -10)] + pts + [(S + 10, S + 10)])
    ImageDraw.Draw(m).polygon(poly, fill=255)
    return m


def icon():
    bg = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(bg).ellipse((8, 8, S - 8, S - 8), fill=DARK)
    left = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    left.paste(watch(AMBER), (-26, -18), half(True).transform(
        (S, S), Image.AFFINE, (1, 0, 26, 0, 1, 18)))
    right = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    right.paste(watch(TEAL), (26, 18), half(False).transform(
        (S, S), Image.AFFINE, (1, 0, -26, 0, 1, -18)))
    out = Image.alpha_composite(bg, left)
    return Image.alpha_composite(out, right)


def main():
    os.makedirs(OUT, exist_ok=True)
    im = icon()
    im.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT, "split2.png"))
    im.resize((256, 256), Image.LANCZOS).save(
        os.path.join(OUT, "split2.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
