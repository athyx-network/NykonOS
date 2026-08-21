from PIL import Image, ImageDraw

def create_chevron(filename):
    # 48x48 image
    img = Image.new('RGBA', (48, 48), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    width = 6
    radius = width // 2
    
    points = [
        (16, 12),
        (28, 24),
        (16, 36)
    ]
    
    # Draw lines
    draw.line(points, fill=(0, 0, 0, 255), width=width, joint="curve")
    
    # Draw rounded caps
    for pt in points:
        x, y = pt
        draw.ellipse([x - radius, y - radius, x + radius, y + radius], fill=(0, 0, 0, 255))
    
    img.save(filename)

create_chevron("src/sys/wp/unlock_sprite.png")
