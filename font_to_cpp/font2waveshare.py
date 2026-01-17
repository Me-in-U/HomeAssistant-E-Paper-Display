# font2waveshare.py (Waveshare e-Paper 디스플레이용 폰트 변환 도구)
# 기능:
#   TTF 폰트 파일을 읽어 Waveshare e-Paper 드라이버에서 사용할 수 있는
#   C 소스 코드(.c)와 헤더 파일(.h) 포맷(sFONT 구조체)으로 변환합니다.
#
# 사용법 예시:
#   python font2waveshare.py --ttf "경로/폰트파일.ttf" --size 64 --name Maple64 --out ./fonts --digits-only --force-width 48 --force-height 72 --include-fonts-h
#
# 주요 옵션:
#   --ttf: 변환할 트루타입 폰트(TTF) 파일 경로
#   --size: 폰트 크기 (픽셀 단위)
#   --name: 생성될 C 배열 및 구조체 이름 (예: Maple64)
#   --out: 출력 파일이 저장될 디렉토리
#   --digits-only: 숫자(0-9)와 일부 기호만 변환할 경우 사용
#   --force-width: 폰트 너비를 특정 값으로 강제 고정 (0이면 자동)
#   --force-height: 폰트 높이를 특정 값으로 강제 고정 (0이면 자동)
#   --include-fonts-h: Waveshare 호환용 sFONT 구조체 정의를 헤더에 포함할지 여부

import argparse, os
from PIL import Image, ImageFont, ImageDraw

ASCII_START = 0x20  # ' ' (공백)
ASCII_END = 0x7E  # '~' (물결표)


def pack_row_to_bytes(row_bits):
    """
    한 행(row)의 픽셀(1/0) 리스트를 바이트(8비트) 단위로 묶어 반환합니다.
    Waveshare 포맷은 MSB(최상위 비트)가 먼저 오는 방식을 사용합니다.
    """
    out = []
    b = 0
    n = 0
    for bit in row_bits:
        b = (b << 1) | (1 if bit else 0)
        n += 1
        if n == 8:  # 8비트가 차면 바이트로 저장
            out.append(b)
            b = 0
            n = 0
    if n != 0:  # 남은 비트 처리
        b = b << (8 - n)
        out.append(b)
    return out


def render_glyph(ch, font, ascent, target_w, target_h, baseline_off=0):
    """
    문자 하나를 렌더링하고 비트맵 데이터를 생성합니다.
    """
    bbox = font.getbbox(ch)
    if bbox is None:
        # 렌더링할 내용이 없는 경우(공백 등) 빈 배열 반환
        return [0] * (((target_w + 7) // 8) * target_h)

    gx0, gy0, gx1, gy1 = bbox
    gW = max(1, gx1 - gx0)  # 글리프 너비
    gH = max(1, gy1 - gy0)  # 글리프 높이

    # 이미지 생성: 흰 배경(255), 검정 글자(0)
    img = Image.new("L", (target_w, target_h), color=255)
    draw = ImageDraw.Draw(img)

    # 수직 정렬 (베이스라인 기준)
    baseline_y = ascent + baseline_off
    y = baseline_y - font.getmetrics()[0]
    
    # 수평 중앙 정렬
    x = (target_w - gW) // 2 - gx0

    draw.text((x, y), ch, font=font, fill=0)  # 검정색으로 텍스트 그리기

    data = []
    total_ones = 0
    for yy in range(target_h):
        # 픽셀 값이 128 미만이면 잉크(검정/1), 아니면 배경(흰색/0)
        row_bits = [1 if img.getpixel((xx, yy)) < 128 else 0 for xx in range(target_w)]
        total_ones += sum(row_bits)
        data.extend(pack_row_to_bytes(row_bits))

    if total_ones == 0:
        # 렌더링 결과가 비어있는 경우 경고 출력
        print(f"[경고] 빈 글리프 감지됨: {repr(ch)} (코드: U+{ord(ch):04X})")
    return data


def calc_fixed_metrics(font):
    """
    모든 ASCII 문자를 순회하며 최대 너비와 높이를 계산하여,
    고정폭 폰트 메트릭을 결정합니다.
    """
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
    
    # 너비는 8의 배수로 맞춤 (바이트 정렬을 위해 권장됨)
    target_w = ((max_w + 6) + 7) // 8 * 8  
    target_h = max(max_h + 4, ascent + descent)
    return ascent, descent, target_w, target_h


def write_header(out_dir, name, width, height, include_fonts_h):
    """
    C 헤더 파일(.h) 생성
    """
    hpath = os.path.join(out_dir, f"{name}.h")
    with open(hpath, "w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <stdint.h>\n\n")
        if include_fonts_h:
            f.write("// Waveshare sFONT 호환 구조체 정의\n")
            f.write(
                "typedef struct {\n  const uint8_t *table;\n  uint16_t Width;\n  uint16_t Height;\n} sFONT;\n\n"
            )
        f.write(f"// 폰트 데이터 테이블 선언\n")
        f.write(f"extern const uint8_t {name}_Table[];\n")
        f.write(f"// 폰트 구조체 선언\n")
        f.write(f"extern const sFONT {name};\n")
    return hpath


def write_source(out_dir, name, width, height, table_bytes):
    """
    C 소스 파일(.c) 생성
    """
    cpath = os.path.join(out_dir, f"{name}.c")
    with open(cpath, "w", encoding="utf-8") as f:
        f.write(f'#include "{name}.h"\n\n')
        f.write(f"const uint8_t {name}_Table[] = {{\n")
        
        # 데이터 배열 쓰기 (보기 좋게 줄바꿈)
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
    ap = argparse.ArgumentParser(description="TTF 폰트를 Waveshare C 포맷으로 변환")
    ap.add_argument("--ttf", required=True, help="TTF 폰트 파일 경로")
    ap.add_argument("--size", type=int, required=True, help="폰트 크기")
    ap.add_argument("--name", default="Font48", help="생성할 폰트 이름 (기본값: Font48)")
    ap.add_argument("--out", default=".", help="출력 디렉토리")
    ap.add_argument("--digits-only", action="store_true", help="숫자와 기호만 변환")
    ap.add_argument("--force-width", type=int, default=0, help="너비 강제 설정 (0: 자동)")
    ap.add_argument("--force-height", type=int, default=0, help="높이 강제 설정 (0: 자동)")
    ap.add_argument("--include-fonts-h", action="store_true", help="sFONT 구조체 정의 포함")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    
    try:
        font = ImageFont.truetype(args.ttf, args.size)
    except IOError:
        print(f"[오류] 폰트 파일을 열 수 없습니다: {args.ttf}")
        return

    ascent, descent, auto_w, auto_h = calc_fixed_metrics(font)

    # 사용자 지정 크기가 있으면 사용, 없으면 자동 계산된 크기 사용
    target_w = args.force_width if args.force_width else auto_w
    target_h = args.force_height if args.force_height else auto_h

    print(
        f"[정보] 메트릭: ascent={ascent}, descent={descent}, 자동크기={auto_w}x{auto_h}, 최종크기={target_w}x{target_h}"
    )
    
    bytes_per_glyph = ((target_w + 7) // 8) * target_h
    total_chars = (ASCII_END - ASCII_START + 1)
    
    print(
        f"[정보] 글자당 {bytes_per_glyph} 바이트, 예상 테이블 크기 ≈ {bytes_per_glyph * total_chars} 바이트"
    )

    table = []
    # 숫자 모드일 때 허용할 문자 목록
    allowed = "0123456789:- " if args.digits_only else None
    
    for code in range(ASCII_START, ASCII_END + 1):
        ch = chr(code)
        if allowed is not None and ch not in allowed:
            # 허용되지 않은 문자는 빈 데이터로 채움
            glyph = [0] * bytes_per_glyph
        else:
            glyph = render_glyph(ch, font, ascent, target_w, target_h, baseline_off=0)
        table.extend(glyph)

    h = write_header(args.out, args.name, target_w, target_h, args.include_fonts_h)
    c = write_source(args.out, args.name, target_w, target_h, table)
    
    print(f"[완료] 헤더 생성됨: {h}")
    print(f"[완료] 소스 생성됨: {c}")
    print(f"[정보] 최종 테이블 크기: {len(table)} 바이트")


if __name__ == "__main__":
    main()
