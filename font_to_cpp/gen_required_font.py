import argparse
import os
from PIL import Image, ImageFont, ImageDraw

# Essential characters for Elevator Display
# Added Weather/Environment related characters
REQUIRED_HANGUL = (
    "0123456789층엘리베이터위치호출대기중현재심야절전"
    "온도습강수확률미세먼지풍속날씨"
    "보통나쁨좋음매우최악"
    "맑음구름많조금흐림비눈안개"
    "%°C.km/h "
)


def generate_c_font(ttf_path, size, out_path):
    font = ImageFont.truetype(ttf_path, size)

    # Calculate byte width
    width = size
    height = size
    bytes_per_row = (width + 7) // 8

    filename = f"Font{size}KR.c"
    filepath = os.path.join(out_path, filename)

    processed_chars = set()  # To avoid duplicates

    with open(filepath, "w", encoding="utf-8") as f:
        f.write('#include "fonts.h"\n\n')
        f.write(f"// Custom Korean Font {size}x{size}\n")
        f.write(f"// Generated from {os.path.basename(ttf_path)}\n\n")

        f.write(f"const CH_CN Font{size}KR_Table[] = {{\n")

        count = 0

        # 1. Add ASCII numbers/symbols if needed, but user asked for "Required Hangul + Integers"
        # The prompt says "Integer part only" and "Layer also 64 font".
        # Maple64 covers numbers well, but to use consistent font style, we can include numbers here too.
        # Let's include basic constraints

        chars_to_gen = sorted(list(set(REQUIRED_HANGUL)))  # Unique sorted

        for char in chars_to_gen:
            # Check if ASCII or Hangul to determine index format
            # Creating 3-byte index for everything for compatibility with Paint_DrawString_CN logic which uses UTF-8 bytes scan
            # But wait, Paint_DrawString_CN logic usually iterates string byte by byte.
            # If byte < 0x80, it might skip or treat as ASCII.

            # However, typical Waveshare library 'Paint_DrawString_CN' handles Chinese/Korean by checking if byte > 0x80.
            # If we want to use 'Paint_DrawString_CN' for numbers too, they need to be treated as CN/KR chars or we need to rely on Standard ASCII logic.
            # Usually Paint_DrawString_CN only handles multi-byte (high bit set) chars.
            # Paint_DrawString_EN handles ASCII.

            # Strategy:
            # - Numbers (0-9): Generate them but they might not be used by DrawString_CN unless we modify the library.
            # - But wait, the user wants "Layer" (Hangul) to be same size.
            # - DrawString_EN uses sFONT structure (table, width, height).
            # - DrawString_CN uses cFONT structure (table with index, size, width, height).

            # If we want to use ONE font file for both, we need to generate both structures or mix them.
            # But existing code uses `Paint_DrawString_EN(..., &Maple64)` and `Paint_DrawString_CN(..., &Font20KR)`.
            # We want to replace Font20KR with Font64KR.

            # The 'cFONT' structure uses a lookup table (index, matrix).
            # So if we put numbers in the cFONT table, we can print them using DrawString_CN if we pass them as wide characters or if the library supports ASCII in CN function.
            # Standard Waveshare 'Paint_DrawString_CN':
            # const char* p_text = pString;
            # while (*p_text != 0) {
            #    if (*p_text <= 0x7F) { ... Paint_DrawChar(...) } else { ... lookup in cFONT ... }
            # }
            # It usually delegate ASCII to Paint_DrawChar which needs an sFONT.

            # So, for 64px Numbers, we should continue using Paint_DrawString_EN with Maple64 (or a new 64px ASCII font).
            # The user is happy with numbers ("Numbers come out well"). User complained about "Layer" (Hangul).

            # So we only need to generate HANGUL characters in the cFONT format.
            # "0123456789" can be skipped here if we use Maple64 for numbers.
            # But user said "Same size". Maple64 is 64px. Our new Hangul font will be 64px. So they will match.

            # Let's only generate Hangul chars.

            # if ord(char) < 0x80:
            #     continue

            # Render character
            image = Image.new("1", (width, height), "white")
            draw = ImageDraw.Draw(image)

            # Center the text
            bbox = font.getbbox(char)
            if bbox:
                w = bbox[2] - bbox[0]
                h = bbox[3] - bbox[1]
                x = (width - w) // 2 - bbox[0]
                y = (height - h) // 2 - bbox[1]

                # Fine tuning for vertical alignment if needed
                # y -= size // 10

                draw.text((x, y), char, font=font, fill="black")
            else:
                draw.text((0, 0), char, font=font, fill="black")

            bytes_data = []
            pixels = image.load()

            for y in range(height):
                current_byte = 0
                for x in range(width):
                    if pixels[x, y] == 0:  # Black pixel
                        current_byte |= 0x80 >> (x % 8)

                    if (x % 8) == 7:
                        bytes_data.append(current_byte)
                        current_byte = 0

                if (width % 8) != 0:
                    bytes_data.append(current_byte)

            # UTF-8 encoding of the character
            utf8_bytes = char.encode("utf-8")

            b0 = utf8_bytes[0]
            b1 = utf8_bytes[1] if len(utf8_bytes) > 1 else 0
            b2 = utf8_bytes[2] if len(utf8_bytes) > 2 else 0

            f.write(f"  {{{{0x{b0:02X}, 0x{b1:02X}, 0x{b2:02X}}}, {{")
            hex_data = [f"0x{b:02X}" for b in bytes_data]
            f.write(", ".join(hex_data))
            f.write("}}")
            f.write(",\n")

            count += 1
            processed_chars.add(char)

        f.write("};\n\n")

        f.write(f"cFONT Font{size}KR = {{\n")
        f.write(f"  Font{size}KR_Table,\n")
        f.write(f"  {count}, /* size */\n")
        f.write(f"  {width}, /* ASCII Width */\n")
        f.write(f"  {width}, /* Width */\n")
        f.write(f"  {height}, /* Height */\n")
        f.write("};\n")

        print(f"Generated {count} characters: {''.join(sorted(processed_chars))}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--ttf", required=True, help="Path to TTF file")
    parser.add_argument("--size", type=int, default=64, help="Font size")
    parser.add_argument("--out", default=".", help="Output directory")
    args = parser.parse_args()

    generate_c_font(args.ttf, args.size, args.out)
