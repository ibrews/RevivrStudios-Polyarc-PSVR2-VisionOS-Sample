from PIL import Image, ImageDraw, ImageFilter
import os, math

OUT = "/tmp/pinchwork_icon"
os.makedirs(OUT, exist_ok=True)
S = 1024
SS = 3  # supersample for crisp anti-aliasing

# ---- BACK: diagonal cobalt -> warm-amber gradient (nods to the two rooms) ----
c1 = (22, 50, 128)    # cobalt (Cobalt Lab)
c2 = (216, 124, 44)   # warm amber (Stone Courtyard)
small = Image.new("RGB", (64, 64))
ps = small.load()
for y in range(64):
    for x in range(64):
        t = (x + y) / 126.0
        ps[x, y] = (int(c1[0]+(c2[0]-c1[0])*t),
                    int(c1[1]+(c2[1]-c1[1])*t),
                    int(c1[2]+(c2[2]-c1[2])*t))
back = small.resize((S, S), Image.LANCZOS).convert("RGBA")
# gentle radial darkening at edges for depth
vig = Image.new("L", (S, S), 0)
vd = ImageDraw.Draw(vig)
vd.ellipse([int(-0.15*S), int(-0.15*S), int(1.15*S), int(1.15*S)], fill=255)
vig = vig.filter(ImageFilter.GaussianBlur(120))
dark = Image.new("RGBA", (S, S), (0, 0, 0, 70))
back = Image.composite(back, Image.alpha_composite(back, dark), vig)
back.save(f"{OUT}/back.png")

# ---- MIDDLE: soft white halo glow (parallaxes behind the mark) ----
mid = Image.new("RGBA", (S, S), (0, 0, 0, 0))
md = ImageDraw.Draw(mid)
r = int(0.34 * S); cx = cy = S // 2
md.ellipse([cx-r, cy-r, cx+r, cy+r], fill=(255, 255, 255, 255))
mid = mid.filter(ImageFilter.GaussianBlur(95))
rr, gg, bb, aa = mid.split()
aa = aa.point(lambda v: int(v * 0.42))
mid = Image.merge("RGBA", (rr, gg, bb, aa))
mid.save(f"{OUT}/middle.png")

# ---- FRONT: pinch ring (thumb+index loop) with a gap + warm spark ----
F = S * SS
front = Image.new("RGBA", (F, F), (0, 0, 0, 0))
fd = ImageDraw.Draw(front)
cxf = cyf = F // 2
ring_r = int(0.30 * F)
ring_w = int(0.085 * F)
bbox = [cxf-ring_r, cyf-ring_r, cxf+ring_r, cyf+ring_r]
# arc leaving a ~80deg gap at the upper-right (the fingertips almost meeting)
fd.arc(bbox, start=125, end=405, fill=(255, 255, 255, 255), width=ring_w)
# rounded cap + warm "pinch spark" at the open end
end_a = math.radians(45)
ex = cxf + int(ring_r * math.cos(end_a))
ey = cyf - int(ring_r * math.sin(end_a))
cap = ring_w // 2
fd.ellipse([ex-cap, ey-cap, ex+cap, ey+cap], fill=(255, 255, 255, 255))
spark = int(ring_w * 0.62)
sx = cxf + int(ring_r * math.cos(math.radians(8)))
sy = cyf - int(ring_r * math.sin(math.radians(8)))
fd.ellipse([sx-spark, sy-spark, sx+spark, sy+spark], fill=(255, 226, 150, 255))
front = front.resize((S, S), Image.LANCZOS)
front.save(f"{OUT}/front.png")

# ---- FLAT composite (for the iOS-style appiconset / fallback) ----
flat = back.copy()
flat.alpha_composite(mid)
flat.alpha_composite(front)
flat.convert("RGB").save(f"{OUT}/flat.png")

print("wrote layers + flat to", OUT, "sizes:", [Image.open(f"{OUT}/"+n).size for n in ("back.png","middle.png","front.png","flat.png")])

# Stash layers + flat in the repo so the visionOS layered-icon chip can wire them in.
import shutil
REPO_SRC = "/Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample/Build/VisionOS/AppIconSource"
os.makedirs(REPO_SRC, exist_ok=True)
for name in ("back", "middle", "front", "flat"):
    shutil.copy(f"{OUT}/{name}.png", f"{REPO_SRC}/{name}.png")
shutil.copy(__file__, f"{REPO_SRC}/gen_icon.py")
print("stashed layers+flat+script in", REPO_SRC)
