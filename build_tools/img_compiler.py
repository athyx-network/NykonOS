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
                    
                    is_sprite = '_sprite' in f.lower() or 'character' in f.lower() or 'icon' in f.lower()
                    if 'icon' in f.lower():
                        # Crop transparent borders using alpha thresholding, except for terminal icon
                        if 'terminal' not in root.lower() and 'terminal' not in f.lower():
                            r, g, b, a = img.split()
                            a_thresh = a.point(lambda p: 255 if p >= 70 else 0)
                            temp_img = Image.merge('RGBA', (r, g, b, a_thresh))
                            bbox = temp_img.getbbox()
                            if bbox:
                                img = img.crop(bbox)
                        # Force icons to 48x48
                        img = img.resize((48, 48), Image.Resampling.LANCZOS)
                    elif not is_sprite:
                        # Fullscreen bg
                        img = img.resize((320, 540), Image.Resampling.LANCZOS)
                    elif 'character' in f.lower():
                        # Auto-scale user characters to fit Flappy Bird (increased size)
                        img = img.resize((58, 42), Image.Resampling.LANCZOS)
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
            elif f.lower().endswith('.ttf'):
                path = os.path.join(root, f)
                try:
                    from PIL import ImageFont, ImageDraw
                    sizes = [(16, '.nfn'), (64, '_large.nfn')]
                    for font_size, ext in sizes:
                        font_height = font_size + 4
                        font = ImageFont.truetype(path, font_size)
                        payload = bytearray(b'NYFN')
                        payload.append(font_height)
                        for i in range(32, 127):
                            char = chr(i)
                            adv_w = int(font.getlength(char))
                            if adv_w > 255: adv_w = 255
                            
                            img = Image.new('L', (adv_w + 20, font_height), 0)
                            draw = ImageDraw.Draw(img)
                            draw.text((0, 0), char, font=font, fill=255)
                            
                            bmp_w = adv_w
                            for x in range(adv_w + 19, -1, -1):
                                col_has_pixel = False
                                for y in range(font_height):
                                    if img.getpixel((x, y)) > 127:
                                        col_has_pixel = True
                                        break
                                if col_has_pixel:
                                    bmp_w = x + 1
                                    break
                            
                            if bmp_w < adv_w: bmp_w = adv_w
                            if bmp_w > 255: bmp_w = 255
                            
                            payload.append(adv_w)
                            payload.append(bmp_w)
                            
                            for y in range(font_height):
                                for x in range(bmp_w):
                                    px = img.getpixel((x, y))
                                    payload.append(px)
                        
                        nfn_path = path[:-4] + ext
                        with open(nfn_path, 'wb') as out:
                            out.write(payload)
                        print(f"Compiled {f} size {font_size} to {os.path.basename(nfn_path)}")
                except Exception as e:
                    print(f"Failed to process {f}: {e}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python img_compiler.py <directory>")
        sys.exit(1)
    compile_images(sys.argv[1])
