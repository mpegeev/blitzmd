#!/usr/bin/env python3
"""Generate the image assets of the samples: samples/gfm/img/* and the
BlitzMD logo samples/logo.png.

Needs Pillow (pip install pillow) and the Inter font in tools/fonts/
(SIL Open Font License, see tools/fonts/OFL.txt). Produces PNG (RGB, RGBA,
indexed with tRNS), a baseline JPEG, an animated GIF, a 24-bit BMP and a few
SVG files. Each picture tests something (a format, transparency, HiDPI,
scaling, a file name); sizes and formats must stay as they are, the samples
and tests rely on them.

Run from anywhere:  python samples/gfm/tools/gen_images.py
The output is the same on every run with the same Pillow version (another
version may antialias a few pixels differently).
"""
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "img"))
LOGO = os.path.normpath(os.path.join(HERE, "..", "..", "logo.png"))
FONT = os.path.join(HERE, "fonts", "InterVariable.ttf")

SS = 4  # supersampling: shapes are drawn this much bigger, then scaled down

# GitHub Primer colors
BLUE = (0x09, 0x69, 0xDA)
PURPLE = (0x82, 0x50, 0xDF)
GREEN = (0x1F, 0x88, 0x3D)
YELLOW = (0xBF, 0x87, 0x00)
RED = (0xCF, 0x22, 0x2E)
FG = (0x1F, 0x23, 0x28)
MUTED = (0x59, 0x63, 0x6E)
BORDER = (0xD1, 0xD9, 0xE0)
CANVAS = (0xF6, 0xF8, 0xFA)
WHITE = (255, 255, 255)


def font(size, weight=600):
    """Inter at `size` pixels and `weight` (100-900), its optical size
    matching (the axes: optical size 14-32, weight)."""
    f = ImageFont.truetype(FONT, size)
    axes = []
    for axis in f.get_variation_axes():
        if axis["name"] == b"Weight":
            axes.append(weight)
        elif axis["name"] == b"Optical size":
            axes.append(min(max(size, axis["minimum"]), axis["maximum"]))
        else:
            axes.append(axis["default"])
    f.set_variation_by_axes(axes)
    return f


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def gradient(w, h, c1, c2, horizontal=True):
    """An RGB image fading from c1 to c2."""
    im = Image.new("RGB", (w, h))
    px = im.load()
    n = (w if horizontal else h) - 1
    for i in range(n + 1):
        col = lerp(c1, c2, i / n if n else 0)
        for j in range(h if horizontal else w):
            px[(i, j) if horizontal else (j, i)] = col
    return im


def shrink(im, size):
    """A supersampled image down to its size (smooth edges)."""
    return im.resize(size, Image.LANCZOS)


def text_center(draw, box, text, fnt, fill, tracking=0):
    """Text centered in box (x0, y0, x1, y1); tracking: extra pixels between letters."""
    x0, y0, x1, y1 = box
    if tracking:
        widths = [draw.textlength(ch, font=fnt) for ch in text]
        total = sum(widths) + tracking * (len(text) - 1)
        l, t, r, b = draw.textbbox((0, 0), text, font=fnt)
        x = x0 + (x1 - x0 - total) / 2
        y = y0 + (y1 - y0 - (b - t)) / 2 - t
        for ch, cw in zip(text, widths):
            draw.text((x, y), ch, font=fnt, fill=fill)
            x += cw + tracking
        return
    l, t, r, b = draw.textbbox((0, 0), text, font=fnt)
    draw.text((x0 + (x1 - x0 - (r - l)) / 2 - l, y0 + (y1 - y0 - (b - t)) / 2 - t), text, font=fnt, fill=fill)


def pill(draw, xy, text, fnt, fill, bg, pad=(8, 4), radius=None):
    """A label on a rounded background, its top left corner at xy."""
    l, t, r, b = draw.textbbox((0, 0), text, font=fnt)
    w, h = r - l + 2 * pad[0], b - t + 2 * pad[1]
    x, y = xy
    draw.rounded_rectangle((x, y, x + w, y + h), radius=h / 2 if radius is None else radius, fill=bg)
    draw.text((x + pad[0] - l, y + pad[1] - t), text, font=fnt, fill=fill)


# --------------------------------------------------------------------------
# The assets
# --------------------------------------------------------------------------
def ruler(w, h, horizontal, label):
    """A ruler: a tick every 10 px (1 px wide, crisp), numbers every 100 or
    200 px, for checking how an image is scaled and scrolled."""
    im = gradient(w, h, BLUE, PURPLE, horizontal)
    d = ImageDraw.Draw(im)
    tick_col = (255, 255, 255)
    length = w if horizontal else h
    for i in range(0, length, 10):
        tick = 56 if i % 100 == 0 else (32 if i % 50 == 0 else 14)
        width = 2 if i % 100 == 0 else 1
        if horizontal:
            d.rectangle((i, 0, i + width - 1, tick - 1), fill=tick_col)
            d.rectangle((i, h - tick, i + width - 1, h - 1), fill=tick_col)
        else:
            d.rectangle((0, i, tick - 1, i + width - 1), fill=tick_col)
            d.rectangle((w - tick, i, w - 1, i + width - 1), fill=tick_col)
    numbers = font(22, 600)
    if horizontal:
        for i in range(200, length, 200):
            d.text((i + 6, 60), str(i), font=numbers, fill=tick_col)
        text_center(d, (0, 0, w, h), label, font(64, 700), tick_col, tracking=2)
    else:
        for i in range(100, length, 100):
            if abs(i - length / 2) > 60:  # (the label is there)
                d.text((62, i + 4), str(i), font=numbers, fill=tick_col)
        text_center(d, (0, 0, w, h), label, font(40, 700), tick_col)
    return im


def transparent_ring():
    """RGBA: a ring fading to transparent inside, transparent outside."""
    size = 160
    big = size * SS
    ring = gradient(big, big, GREEN, BLUE, True).convert("RGBA")
    alpha = Image.new("L", (big, big), 0)
    px = alpha.load()
    c = (big - 1) / 2
    outer, inner = 76 * SS, 50 * SS
    for y in range(big):
        for x in range(big):
            dist = math.hypot(x - c, y - c)
            if dist < outer:
                px[x, y] = 255 if dist > inner else int(255 * (dist / inner) ** 2.2)
    ring.putalpha(alpha)
    im = shrink(ring, (size, size))
    d = ImageDraw.Draw(im)
    text_center(d, (0, 0, size, size), "ALPHA", font(26, 800), FG + (255,), tracking=1)
    return im


def indexed_trns():
    """Indexed PNG, palette transparency (tRNS): four colors at three alpha
    levels, and fully transparent squares between them."""
    w, h = 128, 64
    palette = [RED, YELLOW, GREEN, BLUE, (0, 0, 0)]
    alphas = [255, 170, 85, 255, 0]
    im = Image.new("P", (w, h))
    flat = []
    for col in palette:
        flat += list(col)
    im.putpalette(flat + [0] * (768 - len(flat)))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = 4 if (x // 8 + y // 8) % 2 == 0 else min(3, x // 32)
    im.info["transparency"] = bytes(alphas)
    return im


def hidpi():
    """400x160 meant to show at 200x80: 1 px hairlines tell whether it was
    scaled down well."""
    w, h = 400, 160
    im = Image.new("RGB", (w, h), CANVAS)
    d = ImageDraw.Draw(im)
    for x in range(0, w, 2):
        d.line((x, 0, x, 23), fill=FG)
    for y in range(28, h, 4):
        d.line((0, y, w, y), fill=(0xE4, 0xE9, 0xEE))
    text_center(d, (0, 24, w, h), "2× HiDPI", font(64, 700), BLUE)
    return im


def cyrillic_name():
    """For a file name with Cyrillic letters and spaces."""
    w, h = 240, 120
    im = Image.new("RGB", (w, h), (0xDD, 0xF4, 0xFF))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, w, 5), fill=BLUE)
    d.rectangle((0, h - 6, w, h), fill=BLUE)
    text_center(d, (0, 14, w, 64), "Кириллица", font(34, 700), BLUE)
    text_center(d, (0, 62, w, 104), "и пробелы в имени", font(19, 500), FG)
    return im


def photo():
    """A JPEG "photo": sky, sun, hills."""
    w, h = 400, 240
    bw, bh = w * SS, h * SS
    sky = gradient(bw, bh, (0x3A, 0x78, 0xD6), (0xFF, 0xC9, 0x93), False).convert("RGBA")
    d = ImageDraw.Draw(sky)
    # The sun, with a glow.
    glow = Image.new("RGBA", (bw, bh), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    sx, sy, r = 300 * SS, 92 * SS, 30 * SS
    gd.ellipse((sx - 2.2 * r, sy - 2.2 * r, sx + 2.2 * r, sy + 2.2 * r), fill=(0xFF, 0xF1, 0xC7, 90))
    glow = glow.filter(ImageFilter.GaussianBlur(18 * SS))
    sky = Image.alpha_composite(sky, glow)
    d = ImageDraw.Draw(sky)
    d.ellipse((sx - r, sy - r, sx + r, sy + r), fill=(0xFF, 0xF4, 0xCC, 255))
    # Hills, far to near: each a curve filled below with a vertical fade.
    hills = [
        (150, 20, 70.0, 0.4, (0x7F, 0xB0, 0x8A), (0x5E, 0x94, 0x6C)),
        (172, 24, 48.0, 1.9, (0x3F, 0x9A, 0x52), (0x2A, 0x76, 0x3D)),
        (200, 16, 30.0, 0.8, (0x1E, 0x7A, 0x36), (0x0C, 0x42, 0x1B)),
    ]
    for base, amp, period, phase, top, bottom in hills:
        pts = [(x * SS, (base + amp * math.sin(x / period + phase)) * SS) for x in range(0, w + 1, 2)]
        mask = Image.new("L", (bw, bh), 0)
        ImageDraw.Draw(mask).polygon(pts + [(bw, bh), (0, bh)], fill=255)
        fill = gradient(bw, bh, top, bottom, False).convert("RGBA")
        sky.paste(fill, (0, 0), mask)
    im = shrink(sky.convert("RGB"), (w, h))
    d = ImageDraw.Draw(im, "RGBA")
    pill(d, (10, 10), "JPEG 400×240", font(15, 600), WHITE, (0, 0, 0, 90), pad=(9, 5))
    return im


def bmp():
    """A 24-bit BMP: a two-way color gradient."""
    w, h = 160, 100
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = lerp(lerp(BLUE, PURPLE, x / (w - 1)), lerp((0x1A, 0x7F, 0x37), (0xBF, 0x39, 0x89), x / (w - 1)), y / (h - 1))
    d = ImageDraw.Draw(im)
    text_center(d, (0, 0, w, h), "BMP", font(40, 800), WHITE, tracking=2)
    return im


def traffic_light_frames():
    """Three frames of a traffic light (the lit lamp moves), for GIF."""
    size = 96
    big = size * SS
    lamps = [RED, (0xE3, 0xA0, 0x08), GREEN]
    frames = []
    for active in range(3):
        im = Image.new("RGB", (big, big), WHITE)
        d = ImageDraw.Draw(im)
        d.rounded_rectangle((29 * SS, 3 * SS, 67 * SS, 93 * SS), radius=12 * SS, fill=(0x24, 0x29, 0x2F))
        for i, col in enumerate(lamps):
            cy = (19 + i * 29) * SS
            r = 11 * SS
            d.ellipse((48 * SS - r, cy - r, 48 * SS + r, cy + r), fill=col if i == active else (0x4B, 0x53, 0x5C))
        frames.append(shrink(im, (size, size)))
    return frames


def write_gif(path, frames, delay_ms):
    """One palette for all frames (as the old encoder did), looping forever."""
    strip = Image.new("RGB", (frames[0].width, frames[0].height * len(frames)))
    for i, f in enumerate(frames):
        strip.paste(f, (0, i * f.height))
    pal = strip.quantize(colors=32, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    # The frames cut from the quantized strip (mapping each onto its palette
    # again picks near colors: the white background came out gray).
    h = frames[0].height
    out = [pal.crop((0, i * h, pal.width, (i + 1) * h)) for i in range(len(frames))]
    out[0].save(path, save_all=True, append_images=out[1:], duration=delay_ms, loop=0, optimize=False, disposal=1)


def markdown_mark(draw, x, y, scale, fill):
    """The Markdown mark (Dustin Curtis, public domain), its 208x128 box at
    (x, y) times `scale`: an outline, an M and a down arrow."""
    def p(px, py):
        return (x + px * scale, y + py * scale)
    stroke = 15 * scale
    draw.rounded_rectangle((x + stroke / 2, y + stroke / 2, x + (208 - 7.5) * scale, y + (128 - 7.5) * scale),
                           radius=15 * scale, outline=fill, width=round(stroke))
    m = [(30, 98), (30, 30), (50, 30), (70, 55), (90, 30), (110, 30), (110, 98), (90, 98), (90, 59), (70, 84),
         (50, 59), (50, 98)]
    draw.polygon([p(*q) for q in m], fill=fill)
    arrow = [(155, 98), (125, 65), (145, 65), (145, 30), (165, 30), (165, 65), (185, 65)]
    draw.polygon([p(*q) for q in arrow], fill=fill)


def logo(w=200, h=80):
    """BlitzMD's logo: the Markdown mark and the name on a blue to violet
    card with round corners (transparent outside)."""
    bw, bh = w * SS, h * SS
    card = gradient(bw, bh, (0x3B, 0x5B, 0xF0), (0xA1, 0x3B, 0xE0), True).convert("RGBA")
    # A soft light from the top left.
    light = Image.new("RGBA", (bw, bh), (0, 0, 0, 0))
    ImageDraw.Draw(light).ellipse((-bw * 0.4, -bh * 1.6, bw * 0.7, bh * 0.7), fill=(255, 255, 255, 46))
    card = Image.alpha_composite(card, light.filter(ImageFilter.GaussianBlur(10 * SS)))
    mask = Image.new("L", (bw, bh), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, bw - 1, bh - 1), radius=16 * SS, fill=255)
    card.putalpha(ImageChops.multiply(card.getchannel("A"), mask))
    d = ImageDraw.Draw(card)
    s = h / 80
    markdown_mark(d, 14 * s * SS, 24 * s * SS, 0.25 * s * SS, WHITE)
    im = shrink(card, (w, h))
    d = ImageDraw.Draw(im)
    name_font = font(round(27 * s), 800)
    d.text((76 * s, 18 * s), "BlitzMD", font=name_font, fill=WHITE)
    # The tagline, spaced out to the name's width.
    tagline = "MARKDOWN VIEWER"
    tag = font(round(8.5 * s), 650)
    name_w = d.textlength("BlitzMD", font=name_font)
    widths = [d.textlength(ch, font=tag) for ch in tagline]
    tracking = min(max((name_w - sum(widths)) / (len(tagline) - 1), 0), 2.5 * s)
    x = 77 * s
    for ch, cw in zip(tagline, widths):
        d.text((x, 52 * s), ch, font=tag, fill=(255, 255, 255, 220))
        x += cw + tracking
    return im


def main():
    os.makedirs(OUT, exist_ok=True)
    p = lambda name: os.path.join(OUT, name)

    ruler(2400, 300, True, "WIDE 2400×300").save(p("wide-2400x300.png"))
    ruler(300, 1200, False, "300×1200").save(p("tall-300x1200.png"))
    transparent_ring().save(p("transparent.png"))
    indexed_trns().save(p("indexed-trns.png"))
    hidpi().save(p("hidpi@2x.png"))
    cyrillic_name().save(p("картинка с пробелами.png"))
    photo().save(p("photo.jpg"), quality=85, subsampling=0, progressive=False, optimize=False)
    bmp().save(p("palette.bmp"))
    write_gif(p("animated.gif"), traffic_light_frames(), 700)
    logo().save(LOGO)

    with open(p("badge-build-passing.svg"), "w", encoding="utf-8", newline="\n") as f:
        f.write(badge_svg("build", "passing", 37, 53, "#4c1"))
    with open(p("badge-coverage.svg"), "w", encoding="utf-8", newline="\n") as f:
        f.write(badge_svg("coverage", "97%", 61, 37, "#97ca00"))
    with open(p("gradients.svg"), "w", encoding="utf-8", newline="\n") as f:
        f.write(GRADIENT_SVG)
    with open(p("viewbox-only.svg"), "w", encoding="utf-8", newline="\n") as f:
        f.write(VIEWBOX_SVG)
    print("written to", OUT, "and", LOGO)
    for name in sorted(os.listdir(OUT)):
        print(f"  {os.path.getsize(os.path.join(OUT, name)):>8}  {name}")


# shields.io "flat" style badge layout (text is drawn twice: shadow + face)
BADGE_SVG = """<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="{total}" height="20" role="img" aria-label="{label}: {value}">
  <title>{label}: {value}</title>
  <linearGradient id="s" x2="0" y2="100%">
    <stop offset="0" stop-color="#bbb" stop-opacity=".1"/>
    <stop offset="1" stop-opacity=".1"/>
  </linearGradient>
  <clipPath id="r">
    <rect width="{total}" height="20" rx="3" fill="#fff"/>
  </clipPath>
  <g clip-path="url(#r)">
    <rect width="{lw}" height="20" fill="#555"/>
    <rect x="{lw}" width="{vw}" height="20" fill="{color}"/>
    <rect width="{total}" height="20" fill="url(#s)"/>
  </g>
  <g fill="#fff" text-anchor="middle" font-family="Verdana,Geneva,DejaVu Sans,sans-serif" text-rendering="geometricPrecision" font-size="110">
    <text aria-hidden="true" x="{lx}" y="150" fill="#010101" fill-opacity=".3" transform="scale(.1)">{label}</text>
    <text x="{lx}" y="140" transform="scale(.1)" fill="#fff">{label}</text>
    <text aria-hidden="true" x="{vx}" y="150" fill="#010101" fill-opacity=".3" transform="scale(.1)">{value}</text>
    <text x="{vx}" y="140" transform="scale(.1)" fill="#fff">{value}</text>
  </g>
</svg>
"""


def badge_svg(label, value, lw, vw, color):
    """lw/vw = pixel width of the label/value halves; text is laid out at 10x scale."""
    return BADGE_SVG.format(label=label, value=value, lw=lw, vw=vw, color=color,
                            total=lw + vw, lx=lw * 5, vx=lw * 10 + vw * 5)

GRADIENT_SVG = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200" viewBox="0 0 320 200">
  <defs>
    <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#0969da"/>
      <stop offset="100%" stop-color="#8250df"/>
    </linearGradient>
    <radialGradient id="sun" cx="50%" cy="50%" r="50%">
      <stop offset="0%" stop-color="#fff8c5"/>
      <stop offset="60%" stop-color="#d4a72c"/>
      <stop offset="100%" stop-color="#d4a72c" stop-opacity="0"/>
    </radialGradient>
    <linearGradient id="ground" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#1f883d"/>
      <stop offset="1" stop-color="#116329"/>
    </linearGradient>
  </defs>
  <rect width="320" height="200" rx="12" fill="url(#sky)"/>
  <circle cx="240" cy="70" r="46" fill="url(#sun)"/>
  <path d="M0 150 C 60 110, 120 190, 180 140 S 280 110, 320 150 L 320 200 L 0 200 Z" fill="url(#ground)"/>
  <path d="M20 40 q 20 -20 40 0 t 40 0" fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round" opacity="0.8"/>
  <polygon points="60,150 90,95 120,150" fill="#57606a" stroke="#24292f" stroke-width="2" stroke-linejoin="round"/>
  <rect x="140" y="20" width="60" height="40" rx="6" fill="none" stroke="#ffffff" stroke-width="2" stroke-dasharray="6 4"/>
  <g transform="translate(170 40) rotate(20)">
    <ellipse rx="18" ry="8" fill="#ffffff" fill-opacity="0.6"/>
  </g>
  <text x="16" y="188" font-family="Segoe UI, Helvetica, Arial, sans-serif" font-size="14" font-weight="600" fill="#ffffff">SVG: gradients, paths, text</text>
</svg>
"""

VIEWBOX_SVG = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">
  <!-- No width/height: intrinsic size must come from the viewBox (or the <img> attributes). -->
  <rect x="2" y="2" width="96" height="96" rx="16" fill="#ddf4ff" stroke="#0969da" stroke-width="4"/>
  <path d="M28 52 L44 68 L74 34" fill="none" stroke="#1f883d" stroke-width="10" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
"""

if __name__ == "__main__":
    main()
