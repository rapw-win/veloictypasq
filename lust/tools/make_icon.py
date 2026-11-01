# Generates android/res/drawable/icon.png (accent gradient tile with a bold "lust" wordmark and a star).
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter
S = 192
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
grad = Image.new("RGBA", (S, S))
for y in range(S):
    for x in range(S):
        t = (x + y) / (2 * S - 2)
        a, b = (232, 101, 107), (242, 161, 94)
        grad.putpixel((x, y), tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3)) + (255,))
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle((8, 8, S - 8, S - 8), radius=44, fill=255)
img.paste(grad, (0, 0), mask)
d = ImageDraw.Draw(img)
font = ImageFont.truetype(sys.argv[1], 62)
text = "lust"
w = d.textlength(text, font=font)
shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ImageDraw.Draw(shadow).text(((S - w) / 2, 62), text, font=font, fill=(90, 20, 30, 120))
img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(3)))
d.text(((S - w) / 2, 58), text, font=font, fill=(255, 255, 255, 255))
def star(cx, cy, r):
    pts = []
    import math
    for i in range(8):
        rr = r if i % 2 == 0 else r * 0.38
        a = math.pi / 4 * i - math.pi / 2
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    d.polygon(pts, fill=(255, 255, 255, 230))
star(146, 46, 13); star(46, 146, 8)
img.save(sys.argv[2])
