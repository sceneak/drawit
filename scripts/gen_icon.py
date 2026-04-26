import sys
from PIL import Image

INPUT_PNG = "./assets/app32x32.png"
OUTPUT_H = "./src/gen/icon32x32.inc"
COLS = 12

try:
    img = Image.open(INPUT_PNG).convert("RGBA").resize((32, 32), Image.LANCZOS)
    byte_data = img.tobytes()
    
except FileNotFoundError:
    print(f"{INPUT_PNG} not found.")
    sys.exit()

with open(OUTPUT_H, "w") as f:
    for i, byte in enumerate(byte_data):
        f.write(f"0x{byte:02x}")
        if i < len(byte_data) - 1:
            f.write(", ")
        if (i + 1) % COLS == 0:
            f.write("\n")

print(f"Write {len(byte_data)} bytes to {OUTPUT_H}")