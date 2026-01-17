import argparse
import os
from PIL import Image, ImageFont, ImageDraw

# 한글 음절 범위: AC00 - D7A3
START_CODE = 0xAC00
END_CODE = 0xD7A3

# [설정] 추가로 포함할 문자 목록 (특수기호 등)
# 'full' 모드일 때 기본 세트 외에 추가로 생성됩니다.
ADDITIONAL_CHARS = "℃℉★☆○●◎◇◆□■△▲▽▼→←↑↓↔"

# [설정] 필수 문자 목록 ('lite' 모드용)
# 엘리베이터/날씨 표시에 꼭 필요한 문자만 모아둔 리스트입니다.
REQUIRED_CHARS = (
    "0123456789"
    "층엘리베이터위치호출대기중현재심야절전"
    "온도습강수확률미세먼지풍속날씨"
    "보통나쁨좋음매우최악"
    "맑음구름많조금흐림비눈안개"
    "일월화수목금토"
    "오전후"
    "%°C.km/h :/"
)


def generate_c_font(ttf_path, size, out_path, mode="full"):
    font = ImageFont.truetype(ttf_path, size)

    # 기본 설정
    width = size
    height = size
    ascii_width = int(size * 0.75)  # ASCII는 폭을 좁게 (0.75배)

    filename = f"Font{size}KR.c"
    filepath = os.path.join(out_path, filename)

    print(f"[시작] {filename} 생성 중... (모드: {mode})")

    with open(filepath, "w", encoding="utf-8") as f:
        f.write('#include "fonts.h"\n\n')
        f.write(f"// Korean Font {size}x{size}\n")
        f.write(f"// Generated from {os.path.basename(ttf_path)}\n")
        f.write(f"// Mode: {mode}\n\n")
        f.write(f"const CH_CN Font{size}KR_Table[] = {{\n")

        count = 0
        processed_chars = set()

        # 내부 함수: 문자 렌더링 및 파일 쓰기
        def process_char(char):
            nonlocal count
            
            # ASCII 여부 판단 (기본 ASCII 범위 내)
            is_ascii = ord(char) < 0x80

            image = Image.new("1", (width, height), "white")
            draw = ImageDraw.Draw(image)
            
            bbox = font.getbbox(char)
            if bbox:
                w = bbox[2] - bbox[0]
                h = bbox[3] - bbox[1]
                
                if is_ascii:
                    # ASCII 정렬 로직 (반폭 느낌)
                    if char in ["[", "]"]:
                        x = -bbox[0]
                    else:
                        x = (ascii_width - w) // 2 - bbox[0]
                    
                    if x + bbox[0] < 0: x = -bbox[0]
                    if char in ["[", "]", ","]: x = -bbox[0]

                    y = (height - h) // 2 - bbox[1]
                    if char == ",":
                        padding = max(1, int(height * 0.15))
                        y = height - padding - bbox[3]
                else:
                    # 한글/특수문자 중앙 정렬 로직 (전폭)
                    x = (width - w) // 2 - bbox[0]
                    y = (height - h) // 2 - bbox[1]
                    
                    # (선택사항) 세로 위치 미세 조정이 필요하면 여기에 추가
                    # y -= size // 10 

                draw.text((x, y), char, font=font, fill="black")
            else:
                draw.text((0, 0), char, font=font, fill="black")

            # 비트맵 변환
            bytes_data = []
            pixels = image.load()
            for y in range(height):
                current_byte = 0
                for x in range(width):
                    if pixels[x, y] == 0:
                        current_byte |= 0x80 >> (x % 8)
                    if (x % 8) == 7:
                        bytes_data.append(current_byte)
                        current_byte = 0
                if (width % 8) != 0:
                    bytes_data.append(current_byte)

            # 출력: C 배열 형식
            # ASCII 문자의 경우 인덱스와 0,0을 사용하기도 하지만, 
            # 여기서는 편의상 통합된 cFONT 포맷(UTF-8 인덱스 사용)을 따릅니다.
            # 하지만 Waveshare 라이브러리 호환성을 위해 ASCII 코드로 인덱싱할 수도 있습니다.
            # 기존 gen_korean_font는 ASCII에 대해 {char, 0, 0} 형식을 사용했습니다.
            
            if is_ascii:
                idx_str = f"0x{ord(char):02X}, 0x00, 0x00"
            else:
                utf8 = char.encode("utf-8")
                # 3바이트 UTF-8 (한글 등)
                if len(utf8) == 3:
                     idx_str = f"0x{utf8[0]:02X}, 0x{utf8[1]:02X}, 0x{utf8[2]:02X}"
                # 2바이트 UTF-8 (특수기호 일부)
                elif len(utf8) == 2:
                     idx_str = f"0x{utf8[0]:02X}, 0x{utf8[1]:02X}, 0x00"
                # 1바이트 (ASCII지만 위에서 걸러짐, 안전장치)
                else:
                     idx_str = f"0x{utf8[0]:02X}, 0x00, 0x00"

            f.write(f"  {{{{{idx_str}}}, {{")
            f.write(", ".join([f"0x{b:02X}" for b in bytes_data]))
            f.write("}}")
            f.write(",\n")
            
            count += 1
            processed_chars.add(char)

        # ---------------------------------------------------------
        # 모드별 생성 로직
        # ---------------------------------------------------------
        
        target_chars = []

        if mode == 'lite':
            # lite 모드: 지정된 필수 문자만 생성
            print(f"[정보] 'lite' 모드: 필수 문자열({len(REQUIRED_CHARS)}자)만 생성합니다.")
            
            # 문자열에서 중복 제거 및 정렬
            target_chars = sorted(list(set(REQUIRED_CHARS)))
            
            for char in target_chars:
                if char not in processed_chars:
                    process_char(char)
                    
        else: # mode == 'full'
            print(f"[정보] 'full' 모드: ASCII + 완성형 한글 + 추가 문자 생성")
            
            # 1. ASCII 문자 (0x20 ~ 0x7E)
            # 순서대로 생성
            for code in range(0x20, 0x7F):
                char = chr(code)
                if char not in processed_chars:
                    process_char(char)

            # 2. 완성형 한글 (KS X 1001)
            for code in range(START_CODE, END_CODE + 1):
                char = chr(code)
                
                # KS X 1001 필터링
                is_ks = False
                try:
                    encoded = char.encode("cp949")
                    if len(encoded) == 2:
                        if (0xB0 <= encoded[0] <= 0xC8) and (0xA1 <= encoded[1] <= 0xFE):
                            is_ks = True
                except:
                    pass
                
                if is_ks and char not in processed_chars:
                    process_char(char)
                
                if (code - START_CODE) % 2000 == 0:
                    print(f"...진행 중 ({count}자 생성됨)")

            # 3. 추가 문자 (ADDITIONAL_CHARS)
            for char in ADDITIONAL_CHARS:
                if char not in processed_chars:
                    process_char(char)

        f.write("};\n\n")

        # 구조체 정의
        f.write(f"cFONT Font{size}KR = {{\n")
        f.write(f"  Font{size}KR_Table,\n")
        f.write(f"  {count}, /* size */\n")
        f.write(f"  {ascii_width}, /* ASCII Width */\n")
        f.write(f"  {width}, /* Width */\n")
        f.write(f"  {height}, /* Height */\n")
        f.write("};\n")

    print(f"[완료] 파일 생성됨: {filepath}")
    print(f"[완료] 총 생성된 문자 수: {count}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="한글 및 ASCII 폰트 파일 생성기")
    parser.add_argument("--ttf", required=True, help="TTF 폰트 파일 경로")
    parser.add_argument("--size", type=int, default=24, help="폰트 크기 (기본값: 24)")
    parser.add_argument("--out", default=".", help="출력 디렉토리")
    parser.add_argument("--mode", choices=['full', 'lite'], default='full', 
                        help="생성 모드: 'full'=전체(ASCII+완성형+추가), 'lite'=필수문자만(엘리베이터용)")
    
    args = parser.parse_args()

    generate_c_font(args.ttf, args.size, args.out, args.mode)
