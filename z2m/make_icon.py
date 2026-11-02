"""Draw the gdansk-light device icon (512x512, transparent) for Zigbee2MQTT.

    python3 make_icon.py gdansk-light.png

The gdansk panel, seen in perspective from its luminous side:
119.5 x 29.5 cm, 2.3 cm thick, white frame covering ~2 cm of the
diffuser on every edge. The diffuser shows the colour-temperature range
(warm -> white -> cool).
"""
import math
import sys

from PIL import Image, ImageDraw, ImageFilter

S = 4                        # supersampling factor
W = 512 * S
MARGIN = 0.06                # free border around the drawing (fraction of W)

L, D, T = 119.5, 29.5, 2.3   # length, width, thickness (cm)
FRAME = 2.0                  # frame overlap on the diffuser (cm)

YAW = math.radians(32)       # turn the panel around its vertical axis
PITCH = math.radians(16)     # camera slightly above: the top edge shows
DIST = 150.0                 # camera distance (cm), sets perspective strength

WARM = (255, 170, 80)        # ~2700 K
WHITE = (255, 248, 236)
COOL = (205, 225, 255)       # ~6500 K
FRAME_COL = (248, 249, 251)
EDGE_LONG = (214, 218, 224)  # visible long side (thickness)
EDGE_SHORT = (232, 235, 239) # visible short side
OUTLINE = (120, 126, 136)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def cct(t):
    """Colour along the diffuser, t in [0, 1] from warm to cool."""
    return lerp(WARM, WHITE, t / 0.5) if t < 0.5 else lerp(WHITE, COOL, (t - 0.5) / 0.5)


def transform(p):
    """Panel coordinates (x along the length, y along the width, upwards,
    z thickness: luminous face at z = 0 facing the viewer, back at -T)
    -> camera space (camera on +z, looking towards -z)."""
    x, y, z = p
    # yaw: rotate the panel around its vertical axis
    x, z = x * math.cos(YAW) + z * math.sin(YAW), -x * math.sin(YAW) + z * math.cos(YAW)
    # pitch: camera looking slightly down
    y, z = y * math.cos(PITCH) - z * math.sin(PITCH), y * math.sin(PITCH) + z * math.cos(PITCH)
    return x, y, z


def project(p):
    x, y, z = transform(p)
    k = DIST / (DIST - z)
    return x * k, -y * k


def box(x0, x1, y0, y1, z):
    return [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)]


hx, hy = L / 2, D / 2
front = box(-hx, hx, -hy, hy, 0.0)           # luminous face
back = box(-hx, hx, -hy, hy, -T)
# Side faces, wound so that their normal points outwards
sides = [
    [back[0], back[1], front[1], front[0]],    # y = -hy
    [back[1], back[2], front[2], front[1]],    # x = +hx
    [back[2], back[3], front[3], front[2]],    # y = +hy
    [back[3], back[0], front[0], front[3]],    # x = -hx
]
side_cols = [EDGE_LONG, EDGE_SHORT, EDGE_LONG, EDGE_SHORT]

# Fit the projected drawing into the canvas
pts = [project(p) for p in front + back]
minx, maxx = min(p[0] for p in pts), max(p[0] for p in pts)
miny, maxy = min(p[1] for p in pts), max(p[1] for p in pts)
scale = W * (1 - 2 * MARGIN) / max(maxx - minx, maxy - miny)
ox = W / 2 - (minx + maxx) / 2 * scale
oy = W / 2 - (miny + maxy) / 2 * scale


def px(p):
    x, y = project(p)
    return (ox + x * scale, oy + y * scale)


def visible(quad):
    """True if the face's outward side looks at the camera (3D test)."""
    a, b, c = (transform(q) for q in quad[:3])
    u = [b[k] - a[k] for k in range(3)]
    v = [c[k] - a[k] for k in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    cx, cy, cz = (sum(transform(q)[k] for q in quad) / 4 for k in range(3))
    to_cam = (-cx, -cy, DIST - cz)
    return sum(n[k] * to_cam[k] for k in range(3)) > 0


img = Image.new("RGBA", (W, W), (0, 0, 0, 0))

# Soft warm glow behind the panel
glow = Image.new("RGBA", (W, W), (0, 0, 0, 0))
ImageDraw.Draw(glow).polygon([px(p) for p in front], fill=(255, 215, 160, 140))
img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(16 * S)))

d = ImageDraw.Draw(img)
ow = max(1, int(1.6 * S))

# Thickness: the side faces turned towards the viewer
for quad, col in zip(sides, side_cols):
    if visible(quad):
        d.polygon([px(p) for p in quad], fill=col + (255,), outline=OUTLINE + (255,), width=ow)

# White frame (whole luminous face)
d.polygon([px(p) for p in front], fill=FRAME_COL + (255,), outline=OUTLINE + (255,), width=ow)

# Diffuser inside the frame, gradient along the length, drawn in thin strips
ix0, ix1, iy0, iy1 = -hx + FRAME, hx - FRAME, -hy + FRAME, hy - FRAME
N = 400
for i in range(N):
    a = ix0 + (ix1 - ix0) * i / N
    b = ix0 + (ix1 - ix0) * (i + 1) / N + 0.05
    d.polygon([px(p) for p in box(a, b, iy0, iy1, 0.0)], fill=cct((i + 0.5) / N) + (255,))

img = img.resize((512, 512), Image.LANCZOS)
img.save(sys.argv[1])
