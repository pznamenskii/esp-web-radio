"""Verify every MDI glyph codepoint used by the project exists in the font.

Checks two things against fonts/materialdesignicons-webfont.ttf:
  1. The AUTHORITATIVE codepoint table (user-supplied, from the actual
     Material Design Icons font) - see AUTHORITATIVE below.
  2. Every \\U000Fxxxx escape referenced anywhere in packages/*.yaml, so a
     wrong codepoint introduced later is caught here too.

Prints FOUND/MISSING per codepoint and exits non-zero if anything is missing.
Usage: python tests/font_codepoint_check.py
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / 'fonts' / 'materialdesignicons-webfont.ttf'

# ---------------------------------------------------------------------------
# Authoritative codepoint table (from the actual Material Design Icons font).
# Cross-checked against the MDI cheatsheet: play-circle-outline = F040D,
# cogs = F08D6, radio = F0439, volume-plus = F075D, volume-minus = F075E,
# volume-low/volume-medium = F057F/F0580 (the previous WRONG picks).
# ---------------------------------------------------------------------------
AUTHORITATIVE = {
    'text_hastatus':      0xF07D0,   # mdi-home-assistant
    'text_nav_playing':   0xF040D,   # mdi-play-circle-outline
    'text_nav_settings':  0xF08D6,   # mdi-cogs
    'text_nav_stations':  0xF0439,   # mdi-radio
    'wifi_25':            0xF091F,   # mdi-wifi-strength-1
    'wifi_50':            0xF0922,   # mdi-wifi-strength-2
    'wifi_75':            0xF0925,   # mdi-wifi-strength-3
    'wifi_100':           0xF0928,   # mdi-wifi-strength-4
    'text_wifi':          0xF05A9,   # mdi-wifi
    'text_wifi_cog':      0xF16BE,   # mdi-wifi-cog
    'text_play':          0xF040A,   # mdi-play
    'text_pause':         0xF03E4,   # mdi-pause
    'text_skip_prev':     0xF04AE,   # mdi-skip-previous
    'text_skip_next':     0xF04AD,   # mdi-skip-next
    'text_volume_high':   0xF057E,   # mdi-volume-high
    'text_volume_plus':   0xF075D,   # mdi-volume-plus
    'text_volume_minus':  0xF075E,   # mdi-volume-minus
    'text_volume_off':    0xF0581,   # mdi-volume-off
}


def load_cmap():
    d = FONT.read_bytes()
    num_tables = struct.unpack_from('>H', d, 4)[0]
    cmap_off = None
    for i in range(num_tables):
        rec = 12 + i * 16
        tag = d[rec:rec + 4]
        if tag == b'cmap':
            cmap_off = struct.unpack_from('>I', d, rec + 8)[0]
            break
    assert cmap_off is not None, 'no cmap table found'

    num_sub = struct.unpack_from('>H', d, cmap_off + 2)[0]
    subtables = []
    for i in range(num_sub):
        rec = cmap_off + 4 + i * 8
        pid, eid, soff = struct.unpack_from('>HHI', d, rec)
        subtables.append((cmap_off + soff, pid, eid))

    def parse_fmt4(s):
        seg_x2 = struct.unpack_from('>H', d, s + 6)[0]
        nseg = seg_x2 // 2
        end = [struct.unpack_from('>H', d, s + 14 + 2 * i)[0] for i in range(nseg)]
        p = s + 14 + 2 * nseg
        start = [struct.unpack_from('>H', d, p + 2 * i)[0] for i in range(nseg)]
        p += 2 * nseg
        delta = [struct.unpack_from('>h', d, p + 2 * i)[0] for i in range(nseg)]
        p += 2 * nseg
        ro = [struct.unpack_from('>H', d, p + 2 * i)[0] for i in range(nseg)]

        def has(cp):
            for j in range(nseg):
                if start[j] <= cp <= end[j]:
                    if ro[j] == 0:
                        return True
                    addr = p + 2 * j + ro[j] + 2 * (cp - start[j])
                    gi = (struct.unpack_from('>H', d, addr)[0] + delta[j]) & 0xFFFF
                    return gi != 0
            return False
        return has

    def parse_fmt12(s):
        n = struct.unpack_from('>I', d, s + 12)[0]
        groups = [struct.unpack_from('>III', d, s + 16 + i * 12) for i in range(n)]

        def has(cp):
            for sc, ec, _ in groups:
                if sc <= cp <= ec:
                    return True
            return False
        return has

    def contains(cp):
        for so, _pid, _eid in subtables:
            fmt = struct.unpack_from('>H', d, so)[0]
            if fmt == 4 and parse_fmt4(so)(cp):
                return True
            if fmt == 12 and parse_fmt12(so)(cp):
                return True
        return False

    return contains


def main():
    if not FONT.is_file():
        print(f'MISSING font file: {FONT}')
        return 1

    contains = load_cmap()

    # 1) Authoritative table
    all_cps = dict(AUTHORITATIVE)
    for name, cp in AUTHORITATIVE.items():
        all_cps[f'{name} (authoritative)'] = cp

    # 2) Every \U000Fxxxx escape in packages/*.yaml (substitutions + extras)
    esc_re = re.compile(r'\\U000([0-9A-Fa-f]{5})')
    for yf in sorted((ROOT / 'packages').glob('*.yaml')):
        for m in esc_re.finditer(yf.read_text(encoding='utf-8')):
            cp = int(m.group(1), 16)
            all_cps[f'{yf.name}@{m.start()}'] = cp

    missing = []
    for label, cp in sorted(all_cps.items()):
        ok = contains(cp)
        print('%s  U+%04X  %s' % ('FOUND  ' if ok else 'MISSING', cp, label))
        if not ok:
            missing.append(label)

    if missing:
        print('\n%d codepoint(s) MISSING from the font!' % len(missing))
        return 1
    print('\nAll %d codepoints present in %s' % (len(all_cps), FONT.name))
    return 0


if __name__ == '__main__':
    sys.exit(main())