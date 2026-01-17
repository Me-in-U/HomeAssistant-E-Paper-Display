# font2waveshare.py (극성/임계값 고정, 디버그 카운트 추가)
# 사용법:
#   python font2waveshare.py --ttf "C:/Users/Zoe_Lowell/AppData/Local/Microsoft/Windows/Fonts/Maplestory Light.ttf" --size 64 --name Maple64 --out ./fonts --digits-only --force-width 48 --force-height 72 --include-fonts-h

import argparse, os
from PIL import Image, ImageFont, ImageDraw

ASCII_START = 0x20  # ' '
ASCII_END = 0x7E  # '~'


def pack_row_to_bytes(row_bits):
    out = []
    b = 0
    n = 0
    for bit in row_bits:
        b = (b << 1) | (1 if bit else 0)
        n += 1
        if n == 8:
            out.append(b)
            b = 0
            n = 0
    if n != 0:
        b = b << (8 - n)
        out.append(b)
    return out


def render_glyph(ch, font, ascent, target_w, target_h, baseline_off=0):
    bbox = font.getbbox(ch)
    if bbox is None:
        return [0] * (((target_w + 7) // 8) * target_h)

    gx0, gy0, gx1, gy1 = bbox
    gW = max(1, gx1 - gx0)
    gH = max(1, gy1 - gy0)

    # 흰 배경(255), 검정 글자(0)
    img = Image.new("L", (target_w, target_h), color=255)
    draw = ImageDraw.Draw(img)

    # 수직 정렬(베이스라인 근사)
    baseline_y = ascent + baseline_off
    y = baseline_y - font.getmetrics()[0]
    # 수평 중앙정렬
    x = (target_w - gW) // 2 - gx0

    draw.text((x, y), ch, font=font, fill=0)  # 검정

    data = []
    total_ones = 0
    for yy in range(target_h):
        # p<128 -> 잉크(검정)=1, 배경(흰)=0
        row_bits = [1 if img.getpixel((xx, yy)) < 128 else 0 for xx in range(target_w)]
        total_ones += sum(row_bits)
        data.extend(pack_row_to_bytes(row_bits))

    if total_ones == 0:
        print(f"[warn] empty glyph {repr(ch)} (U+{ord(ch):04X})")
    return data


def calc_fixed_metrics(font):
    ascent, descent = font.getmetrics()
    max_w = 0
    max_h = 0
    for code in range(ASCII_START, ASCII_END + 1):
        ch = chr(code)
        bbox = font.getbbox(ch)
        if bbox:
            w = bbox[2] - bbox[0]
            h = bbox[3] - bbox[1]
            if w > max_w:
                max_w = w
            if h > max_h:
                max_h = h
    target_w = ((max_w + 6) + 7) // 8 * 8  # 8의 배수
    target_h = max(max_h + 4, ascent + descent)
    return ascent, descent, target_w, target_h


def write_header(out_dir, name, width, height, include_fonts_h):
    hpath = os.path.join(out_dir, f"{name}.h")
    with open(hpath, "w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <stdint.h>\n\n")
        if include_fonts_h:
            f.write("// Waveshare sFONT 호환 선언\n")
            f.write(
                "typedef struct {\n  const uint8_t *table;\n  uint16_t Width;\n  uint16_t Height;\n} sFONT;\n\n"
            )
        f.write(f"extern const uint8_t {name}_Table[];\n")
        f.write(f"extern const sFONT {name};\n")
    return hpath


def write_source(out_dir, name, width, height, table_bytes):
    cpath = os.path.join(out_dir, f"{name}.c")
    with open(cpath, "w", encoding="utf-8") as f:
        f.write(f'#include "{name}.h"\n\n')
        f.write(f"const uint8_t {name}_Table[] = {{\n")
        for i, b in enumerate(table_bytes):
            if i % 12 == 0:
                f.write("  ")
            f.write(f"0x{b:02X}, ")
            if (i % 12) == 11:
                f.write("\n")
        if len(table_bytes) % 12 != 0:
            f.write("\n")
        f.write("};\n\n")
        f.write(f"const sFONT {name} = {{ {name}_Table, {width}, {height} }};\n")
    return cpath


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf", required=True)
    ap.add_argument("--size", type=int, required=True)
    ap.add_argument("--name", default="Font48")
    ap.add_argument("--out", default=".")
    ap.add_argument("--digits-only", action="store_true")
    ap.add_argument("--force-width", type=int, default=0)
    ap.add_argument("--force-height", type=int, default=0)
    ap.add_argument("--include-fonts-h", action="store_true")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    font = ImageFont.truetype(args.ttf, args.size)
    ascent, descent, auto_w, auto_h = calc_fixed_metrics(font)

    target_w = args.force_width if args.force_width else auto_w
    target_h = args.force_height if args.force_height else auto_h

    print(
        f"[i] metrics: ascent={ascent}, descent={descent}, auto={auto_w}x{auto_h}, target={target_w}x{target_h}"
    )
    bytes_per_glyph = ((target_w + 7) // 8) * target_h
    print(
        f"[i] bytes/glyph={bytes_per_glyph}, total_table≈{bytes_per_glyph * (ASCII_END-ASCII_START+1)} bytes"
    )

    table = []
    allowed = "0123456789:- " if args.digits_only else None
    for code in range(ASCII_START, ASCII_END + 1):
        ch = chr(code)
        if allowed is not None and ch not in allowed:
            glyph = [0] * bytes_per_glyph
        else:
            glyph = render_glyph(ch, font, ascent, target_w, target_h, baseline_off=0)
        table.extend(glyph)

    h = write_header(args.out, args.name, target_w, target_h, args.include_fonts_h)
    c = write_source(args.out, args.name, target_w, target_h, table)
    print(f"[ok] generated: {h}")
    print(f"[ok] generated: {c}")
    print(f"[info] bytes/glyph={bytes_per_glyph}, total_table≈{len(table)} bytes")


if __name__ == "__main__":
    main()
