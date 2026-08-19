import sys
import os
from PIL import Image, ImageOps
import struct

def compile_images(directory):
    for root, dirs, files in os.walk(directory):
        for f in files:
            if f.lower().endswith(('.jpg', '.jpeg', '.png')):
                path = os.path.join(root, f)
                try:
                    img = Image.open(path)
                    img = img.convert('RGBA')
                    
                    is_sprite = '_sprite' in f.lower() or 'character' in f.lower()
                    if not is_sprite:
                        # Fullscreen bg
                        img = img.resize((320, 540), Image.Resampling.LANCZOS)
                    elif 'character' in f.lower():
                        # Auto-scale user characters to fit Flappy Bird (increased size)
                        img = img.resize((45, 33), Image.Resampling.LANCZOS)
                    width, height = img.size
                    
                    payload = bytearray()
                    payload.extend(b'NYKN')
                    payload.extend(struct.pack('<H', width))
                    payload.extend(struct.pack('<H', height))
                    
                    for y in range(height):
                        for x in range(width):
                            pixel = img.getpixel((x, y))
                            r, g, b = pixel[:3]
                            a = pixel[3] if len(pixel) > 3 else 255
                            
                            if is_sprite and a < 128:
                                # Transparent pixel -> Magic Magenta (0xFFFF00FF)
                                val = 0xFFFF00FF
                            else:
                                val = 0xFF000000 | (b << 16) | (g << 8) | r
                            payload.extend(struct.pack('<I', val))
                            
                    with open(path, 'wb') as out:
                        out.write(payload)
                    print(f"Compiled {f}: {width}x{height}")
                except Exception as e:
                    print(f"Failed to process {f}: {e}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python img_compiler.py <directory>")
        sys.exit(1)
    compile_images(sys.argv[1])
