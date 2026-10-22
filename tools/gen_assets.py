#!/usr/bin/env python3
"""
Generates Cairn's original brand and UI-sound assets.

  python tools/gen_assets.py

Requires: Pillow  (pip install pillow)

Outputs
  assets/icons/cairn.ico, cairn_injector.ico, logo.png, logo_injector.png, logo.svg, wordmark.png
  assets/sounds/*.wav
  src/core/SoundData.inl      (WAVs embedded into the client DLL)

Everything here is synthesized / drawn from scratch -- no third-party
artwork or audio is used, so the outputs are released as CC0 (see
assets/sounds/LICENSE.txt). The logo geometry mirrors Brand::drawLogo()
in src/shared/Brand.cpp -- keep them in sync if you change one.
"""
import io
import math
import pathlib
import struct
import wave

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
ICONS = ROOT / "assets" / "icons"
SOUNDS = ROOT / "assets" / "sounds"
INL = ROOT / "src" / "core" / "SoundData.inl"

# ---- palette (keep in sync with src/shared/UiTheme.h) -----------------------
BADGE = (16, 20, 21)          # #101415  Bg
BORDER = (53, 67, 71)         # #354347  BorderStrong
STONE_LOW = (92, 115, 112)    # #5C7370  base stone (slate)
STONE_MID = (61, 183, 163)    # #3DB7A3  accent (teal)
STONE_TOP = (232, 180, 90)    # #E8B45A  accent2 (lantern amber)
TEXT = (231, 238, 236)        # #E7EEEC
DARK = (6, 32, 27)            # #06201B  on-accent

# The mark: three stacked, slightly offset stones (a cairn / trail marker).
# Normalised to a 1x1 badge centred on (0,0): (cx, cy, half_w, half_h, colour).
# Mirrored in Brand::drawLogo() (src/shared/Brand.cpp) -- keep both in sync.
STONES = [
    (0.000,  0.200, 0.300, 0.102, STONE_LOW),
    (0.028, -0.022, 0.220, 0.091, STONE_MID),
    (-0.013, -0.222, 0.131, 0.080, STONE_TOP),
]
BADGE_RADIUS = 0.22


# =============================================================================
#  Logo / icons
# =============================================================================
def draw_logo(size, injector=False, badge=True):
    ss = 4
    S = size * ss
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    if badge:
        inset = int(0.02 * S)
        d.rounded_rectangle(
            [inset, inset, S - 1 - inset, S - 1 - inset],
            radius=int(BADGE_RADIUS * S),
            fill=BADGE + (255,),
            outline=BORDER + (255,),
            width=max(2, int(0.018 * S)),
        )

    cx = cy = S / 2.0
    for x, y, hw, hh, col in STONES:
        d.rounded_rectangle(
            [cx + (x - hw) * S, cy + (y - hh) * S, cx + (x + hw) * S, cy + (y + hh) * S],
            radius=hh * S,
            fill=col + (255,),
        )

    if injector:
        # Small accent "load" badge (down arrow) in the lower-right corner.
        bx, by = cx + 0.325 * S, cy + 0.325 * S
        br = 0.15 * S
        d.ellipse([bx - br, by - br, bx + br, by + br], fill=STONE_MID + (255,),
                  outline=BADGE + (255,), width=max(2, int(0.025 * S)))
        aw = int(0.045 * S)
        d.line([(bx, by - 0.085 * S), (bx, by + 0.065 * S)], fill=DARK + (255,), width=aw)
        d.line([(bx - 0.07 * S, by - 0.005 * S), (bx, by + 0.075 * S)], fill=DARK + (255,), width=aw)
        d.line([(bx + 0.07 * S, by - 0.005 * S), (bx, by + 0.075 * S)], fill=DARK + (255,), width=aw)

    return img.resize((size, size), Image.LANCZOS)


def logo_svg():
    r = lambda v: round(v * 1000, 1)
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 1000" width="512" height="512">',
             '  <rect x="20" y="20" width="960" height="960" rx="%d" fill="#%02x%02x%02x" stroke="#%02x%02x%02x" stroke-width="18"/>'
             % (BADGE_RADIUS * 1000, *BADGE, *BORDER)]
    for x, y, hw, hh, col in STONES:
        parts.append('  <rect x="%s" y="%s" width="%s" height="%s" rx="%s" fill="#%02x%02x%02x"/>'
                     % (r(0.5 + x - hw), r(0.5 + y - hh), r(2 * hw), r(2 * hh), r(hh), *col))
    parts.append('</svg>')
    return "\n".join(parts) + "\n"


def find_font():
    for f in ("/usr/share/fonts/truetype/lato/Lato-Semibold.ttf",
              "/usr/share/fonts/truetype/google-fonts/Poppins-SemiBold.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
        if pathlib.Path(f).exists():
            return f
    return None


def draw_wordmark(height=160):
    """Horizontal lock-up: badge-less mark + 'cairn' in light text."""
    mark = draw_logo(height, badge=False)
    font_path = find_font()
    if font_path is None:
        return mark
    font = ImageFont.truetype(font_path, int(height * 0.72))
    text = "cairn"
    track = int(height * 0.04)
    widths = [font.getbbox(c)[2] for c in text]
    tw = sum(widths) + track * (len(text) - 1)
    pad = int(height * 0.18)
    img = Image.new("RGBA", (height + pad + tw + pad, height), (0, 0, 0, 0))
    img.paste(mark, (0, 0), mark)
    d = ImageDraw.Draw(img)
    x = height + pad
    ty = int(height * 0.5 - font.getbbox("c")[3] * 0.5 - font.getbbox("c")[1] * 0.5)
    for c, w in zip(text, widths):
        d.text((x, ty), c, font=font, fill=TEXT + (255,))
        x += w + track
    return img


def write_icons():
    ICONS.mkdir(parents=True, exist_ok=True)
    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    draw_logo(256).save(ICONS / "cairn.ico", format="ICO", sizes=sizes)
    draw_logo(256, injector=True).save(ICONS / "cairn_injector.ico", format="ICO", sizes=sizes)
    draw_logo(512).save(ICONS / "logo.png")
    draw_logo(512, injector=True).save(ICONS / "logo_injector.png")
    draw_wordmark(160).save(ICONS / "wordmark.png")
    (ICONS / "logo.svg").write_text(logo_svg(), encoding="utf-8")
    print("icons written")


# =============================================================================
#  UI sounds (22.05 kHz, 16-bit, mono)
# =============================================================================
SR = 22050


def segment(freq_fn, dur, amp, harmonics=((1, 1.0),), decay=4.0):
    """One enveloped tone. freq_fn maps normalized time [0,1] -> Hz."""
    n = int(SR * dur)
    norm = sum(a for _, a in harmonics)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / SR
        f = freq_fn(t / dur)
        phase += 2.0 * math.pi * f / SR
        s = sum(a * math.sin(h * phase) for h, a in harmonics) / norm
        attack = min(1.0, t / 0.004)
        release = min(1.0, max(0.0, (dur - t) / 0.010))
        env = attack * release * math.exp(-decay * t / dur)
        out.append(s * amp * env)
    return out


def mix(parts):
    """parts: list of (start_seconds, samples)."""
    total = max(int(SR * st) + len(smp) for st, smp in parts)
    buf = [0.0] * total
    for st, smp in parts:
        o = int(SR * st)
        for i, v in enumerate(smp):
            buf[o + i] += v
    peak = max(1e-9, max(abs(v) for v in buf))
    scale = min(1.0, 0.6 / peak) if peak > 0.6 else 1.0
    return [v * scale for v in buf]


def to_wav_bytes(samples):
    bio = io.BytesIO()
    with wave.open(bio, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, v)) * 32767)) for v in samples))
    return bio.getvalue()


def const(f):
    return lambda t: f


def sweep(a, b):
    return lambda t: a + (b - a) * t


def build_sounds():
    H2 = ((1, 1.0), (2, 0.25))
    return {
        "click": mix([(0.0, segment(sweep(1500, 1100), 0.045, 0.40, H2, 5.0))]),
        "toggle_on": mix([
            (0.00, segment(const(700), 0.070, 0.38, H2, 3.5)),
            (0.045, segment(const(1050), 0.090, 0.38, H2, 3.5)),
        ]),
        "toggle_off": mix([
            (0.00, segment(const(900), 0.070, 0.34, H2, 3.5)),
            (0.045, segment(const(600), 0.090, 0.34, H2, 3.5)),
        ]),
        "tab": mix([(0.0, segment(const(1000), 0.040, 0.30, ((1, 1.0),), 5.0))]),
        "open": mix([(0.0, segment(sweep(380, 760), 0.150, 0.38, ((1, 1.0), (2, 0.30)), 3.0))]),
        "close": mix([(0.0, segment(sweep(760, 380), 0.130, 0.32, ((1, 1.0), (2, 0.30)), 3.0))]),
        "error": mix([
            (0.00, segment(const(240), 0.090, 0.42, ((1, 1.0), (3, 0.30)), 2.5)),
            (0.130, segment(const(240), 0.090, 0.42, ((1, 1.0), (3, 0.30)), 2.5)),
        ]),
    }


def c_array(name, data):
    rows = []
    for i in range(0, len(data), 16):
        rows.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    return "inline constexpr unsigned char %s[] = {\n%s\n};\n" % (name, "\n".join(rows))


def write_sounds():
    SOUNDS.mkdir(parents=True, exist_ok=True)
    sounds = build_sounds()
    inl = [
        "// Generated by tools/gen_assets.py -- do not edit by hand.\n",
        "// Original synthesized UI sounds, released as CC0 (see assets/sounds/LICENSE.txt).\n",
        "#pragma once\n\nnamespace SoundData\n{\n",
    ]
    for key, samples in sounds.items():
        data = to_wav_bytes(samples)
        (SOUNDS / (key + ".wav")).write_bytes(data)
        camel = "".join(p.capitalize() for p in key.split("_"))
        inl.append(c_array("k" + camel, data))
        print("  %-11s %5d bytes  %.0f ms" % (key, len(data), 1000.0 * len(samples) / SR))
    inl.append("} // namespace SoundData\n")
    INL.write_text("\n".join(inl), encoding="utf-8")
    print("sounds written")


if __name__ == "__main__":
    write_icons()
    write_sounds()
