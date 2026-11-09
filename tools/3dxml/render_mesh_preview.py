"""Orthographic research preview of decoded triangles; requires Pillow."""

import argparse
import json
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


def unit(v):
    length = math.sqrt(sum(x * x for x in v))
    return tuple(x / length for x in v)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    scale = 2
    image = Image.new("RGB", (1600 * scale, 1000 * scale), "#101925")
    draw = ImageDraw.Draw(image)
    font_path = Path("C:/Windows/Fonts/segoeui.ttf")
    font = lambda size: ImageFont.truetype(str(font_path), size * scale) if font_path.exists() else ImageFont.load_default()
    draw.text((65 * scale, 40 * scale), "Decoded 3DXML sample meshes", font=font(32), fill="#edf4fc")
    draw.text((65 * scale, 90 * scale), "Original vertices and triangle references  |  Derived face normals", font=font(19), fill="#9fb2c8")
    view = unit((1.4, -2.1, 1.3))
    right = unit(cross((0, 0, 1), view))
    up = cross(view, right)
    light = unit((-0.5, -1, 2))
    items = [("ISO_2338_5x25_Zyl_Stift", "Dowel pin", (83, 178, 199)),
             ("Distanzring_1_R", "Spacer ring", (126, 159, 226))]
    for panel, (stem, title, color) in enumerate(items):
        mesh = json.loads((args.directory / (stem + ".mesh.json")).read_text())
        # Display-only camera presentation: make these axial-X parts upright.
        # OBJ/scene coordinates retain native XYZ; never bake this into import.
        points = [(p[1], p[2], p[0]) for p in mesh["positions_serialized"]]
        center = tuple((min(p[i] for p in points) + max(p[i] for p in points)) / 2 for i in range(3))
        centered = [tuple(p[i] - center[i] for i in range(3)) for p in points]
        projected = [(dot(p, right), dot(p, up), dot(p, view)) for p in centered]
        xmin, xmax = min(p[0] for p in projected), max(p[0] for p in projected)
        ymin, ymax = min(p[1] for p in projected), max(p[1] for p in projected)
        zoom = min(620 / (xmax - xmin), 650 / (ymax - ymin)) * scale
        cx, cy = (400 + panel * 800) * scale, 510 * scale
        xy = [(cx + (p[0] - (xmin + xmax) / 2) * zoom,
               cy - (p[1] - (ymin + ymax) / 2) * zoom) for p in projected]
        triangles = [tri for face in mesh["finest_faces"] for tri in face["triangles"]]
        visible = []
        for tri in triangles:
            a, b, c = [points[i] for i in tri]
            normal = unit(cross(tuple(b[i] - a[i] for i in range(3)), tuple(c[i] - a[i] for i in range(3))))
            if dot(normal, view) <= 0:
                continue
            intensity = 0.3 + 0.7 * max(0, dot(normal, light))
            fill = tuple(round(channel * intensity) for channel in color)
            visible.append((sum(projected[i][2] for i in tri), tri, fill))
        for _, tri, fill in sorted(visible):
            draw.polygon([xy[i] for i in tri], fill=fill,
                         outline=tuple(round(channel * 0.7) for channel in fill), width=1)
        draw.text(((70 + panel * 800) * scale, 850 * scale), title, font=font(27), fill="#edf4fc")
        draw.text(((70 + panel * 800) * scale, 895 * scale),
                  f"{len(points)} vertex records / {len(triangles)} triangles", font=font(20), fill="#9fb2c8")
    image.resize((1600, 1000), Image.Resampling.LANCZOS).save(args.directory / "decoded_meshes.png")


if __name__ == "__main__":
    main()
