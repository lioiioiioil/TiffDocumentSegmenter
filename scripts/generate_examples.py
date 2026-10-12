"""Create a synthetic document; no scanned or personal content is used."""
from pathlib import Path
from PIL import Image, ImageDraw
import random

out = Path(__file__).resolve().parents[1]/"examples"
out.mkdir(exist_ok=True)
im = Image.new("L", (640, 800), 250)
d = ImageDraw.Draw(im)
d.text((40, 28), "SYNTHETIC DOCUMENT - SEGMENTATION DEMO", fill=15)
for y in range(80, 720, 24):
    for x in range(40, 290, 32):
        d.text((x, y), "abc", fill=20)
for y in range(520, 720, 24):
    for x in range(360, 590, 32):
        d.text((x, y), "xyz", fill=20)
rng = random.Random(42)
for y in range(96, 440):
    for x in range(350, 600):
        im.putpixel((x, y), rng.randint(20, 235))
im.save(out/"document.tif", compression="raw")
print(f"Created {out/'document.tif'}")
