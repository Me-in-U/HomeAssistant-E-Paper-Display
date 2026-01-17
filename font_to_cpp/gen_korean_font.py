import argparse
import os
from PIL import Image, ImageFont, ImageDraw

# Hangul Syllables range: AC00 - D7A3
START_CODE = 0xAC00
END_CODE = 0xD7A3


def generate_c_font(ttf_path, size, out_path):
    font = ImageFont.truetype(ttf_path, size)

    # Calculate byte width
    # We enforce fixed width/height for the struct
    width = size
    height = size
    bytes_per_row = (width + 7) // 8
    matrix_size = bytes_per_row * height

    filename = f"font{size}KR.c"
    filepath = os.path.join(out_path, filename)

    with open(filepath, "w", encoding="utf-8") as f:
        f.write('#include "fonts.h"\n\n')
        f.write(f"// Korean Font {size}x{size}\n")
        f.write(f"// Generated from {os.path.basename(ttf_path)}\n\n")

        f.write(f"const CH_CN Font{size}KR_Table[] = {{\n")

        count = 0

        # 1. Generate ASCII characters (0x20 - 0x7E)
        # This allows numbers and English to be rendered with the same font
        # Increase width ratio to 0.75 to accommodate wider chars like 'W'
        ascii_width = int(size * 0.75)

        for code in range(0x20, 0x7F):
            char = chr(code)

            # Render character
            image = Image.new("1", (width, height), "white")
            draw = ImageDraw.Draw(image)

            bbox = font.getbbox(char)
            if bbox:
                w = bbox[2] - bbox[0]
                h = bbox[3] - bbox[1]

                # Center in the ASCII width
                if char == "[" or char == "]":
                    # Left align the ink to the start of the cell
                    x = -bbox[0]
                else:
                    x = (ascii_width - w) // 2 - bbox[0]

                # Prevent left clipping for wide characters
                if x + bbox[0] < 0:
                    x = -bbox[0]

                # Special handling for brackets and comma to reduce whitespace
                # Align LEFT for all, and we will reduce the advance width in the C code
                if char in ["[", "]", ","]:
                    x = 0 - bbox[0]
                    # Optional: Add 1px padding for aesthetics if needed, but 0 is safest for "removing whitespace"

                y = (height - h) // 2 - bbox[1]

                # Fix vertical alignment for comma (prevent it from floating in middle)
                if char == ",":
                    # Align bottom of glyph to bottom of cell with small padding
                    # y + bbox[3] = height - padding
                    padding = max(1, int(height * 0.15))
                    y = height - padding - bbox[3]

                draw.text((x, y), char, font=font, fill="black")
            else:
                draw.text((0, 0), char, font=font, fill="black")

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

            # ASCII index format: {char, 0, 0}
            f.write(f"  {{{{0x{code:02X}, 0x00, 0x00}}, {{")
            hex_data = [f"0x{b:02X}" for b in bytes_data]
            f.write(", ".join(hex_data))
            f.write("}}")
            f.write(",\n")
            count += 1

        # 2. Generate Hangul characters
        for code in range(START_CODE, END_CODE + 1):
            char = chr(code)

            # Filter for KS X 1001 (Common 2350 Hangul syllables)
            # On Windows, 'euc-kr' might be aliased to 'cp949' which includes all Hangul.
            # We check the CP949 byte range. KS X 1001 Hangul is 0xB0A1 - 0xC8FE.
            try:
                encoded = char.encode("cp949")
                if len(encoded) != 2:
                    continue
                # Check if lead byte is in KS X 1001 Hangul range (0xB0 - 0xC8)
                if not (0xB0 <= encoded[0] <= 0xC8):
                    continue
                # Check if trail byte is in KS X 1001 range (0xA1 - 0xFE)
                if not (0xA1 <= encoded[1] <= 0xFE):
                    continue
            except UnicodeEncodeError:
                continue

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
                # Adjust y for baseline if needed, but centering usually works for block chars
                # For Hangul, simple centering is often okay, or use fixed offset
                # Let's try simple centering first
                draw.text((x, y), char, font=font, fill="black")
            else:
                draw.text((0, 0), char, font=font, fill="black")

            # Convert to bytes
            # GUI_Paint expects: MSB first, row by row
            # "if (*ptr & (0x80 >> (i % 8)))" -> 0x80 is left-most pixel

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
            if len(utf8_bytes) != 3:
                continue  # Should be 3 bytes for Korean

            f.write(
                f"  {{{{0x{utf8_bytes[0]:02X}, 0x{utf8_bytes[1]:02X}, 0x{utf8_bytes[2]:02X}}}, {{"
            )

            hex_data = [f"0x{b:02X}" for b in bytes_data]
            f.write(", ".join(hex_data))

            f.write("}}")

            if code != END_CODE:
                f.write(",\n")
            else:
                f.write("\n")

            count += 1
            if count % 100 == 0:
                print(f"Processed {count} characters...")

        f.write("};\n\n")

        f.write(f"cFONT Font{size}KR = {{\n")
        f.write(f"  Font{size}KR_Table,\n")
        f.write(f"  {count}, /* size */\n")
        f.write(f"  {ascii_width}, /* ASCII Width (not used for KR) */\n")
        f.write(f"  {width}, /* Width */\n")
        f.write(f"  {height}, /* Height */\n")
        f.write("};\n")

    print(f"Done! Generated {filepath} with {count} characters.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--ttf", required=True, help="Path to TTF file")
    parser.add_argument("--size", type=int, default=24, help="Font size (default 24)")
    parser.add_argument("--out", default=".", help="Output directory")
    args = parser.parse_args()

    generate_c_font(args.ttf, args.size, args.out)
