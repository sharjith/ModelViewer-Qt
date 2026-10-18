"""Render decoded sample triangles with XML placements; research illustration.

Requires NumPy and Pillow. Uses derived face normals and triangle-depth painter
ordering, not a reference viewer or a production depth-buffer renderer.
"""
import argparse
import json
from pathlib import Path
import runpy
import zipfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--occurrences", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    args = parser.parse_args()
    args.output_directory.mkdir(parents=True, exist_ok=True)
    api = runpy.run_path(str(Path(__file__).with_name("decode_representation.py")))
    scene = json.loads(args.occurrences.read_text())
    placements = {}
    for occurrence in scene["occurrences"]:
        if not occurrence["visible"]:
            continue
        for rep in occurrence["representations"]:
            if rep["format"] != "UVR":
                continue
            name = rep["associatedFile"].removeprefix("urn:3DXML:")
            placements.setdefault(name, []).append(np.asarray(occurrence["world_matrix_rows"]))
    # Right-handed orthographic research camera in unchanged source XYZ.
    view = np.asarray((1.0, -1.7, 1.0))
    view /= np.linalg.norm(view)
    right = np.cross((0, 0, 1), view)
    right /= np.linalg.norm(right)
    up = np.cross(view, right)
    basis = np.stack((right, up, view), axis=1)
    light = np.asarray((-0.4, -1.0, 2.0))
    light /= np.linalg.norm(light)
    panels = [[], []]
    manifests = []
    with zipfile.ZipFile(args.archive) as archive:
        for name, matrices in placements.items():
            print("Preview decode:", name, flush=True)
            data = archive.read(name)
            entry = next(e for e in api["compound_streams"](data) if e["name"] == "Skeleton")
            housing = any(key in name for key in ("Kupplungsgehaeuse_FINALPART", "Getriebedeckel_FINALPART", "Mittelgehaeuse_FINALPART"))
            info = {"member": name, "visible_occurrences": len(matrices), "meshes": 0,
                    "triangles_per_occurrence": 0, "is_housing": housing}
            def visit(leaf, mesh, groups, styles):
                points = np.asarray(mesh["positions_serialized"], dtype=np.float64)
                indices = np.asarray([tri for face in mesh["finest_faces"] for tri in face["triangles"]], dtype=np.int32)
                info["meshes"] += 1
                info["triangles_per_occurrence"] += len(indices)
                for matrix in matrices:
                    world = points @ matrix[:3, :3].T + matrix[:3, 3]
                    triangles = world[indices]
                    normals = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
                    front = normals @ view > 0
                    projected = (triangles[front] @ basis).astype(np.float32)
                    ns = normals[front]
                    ns /= np.linalg.norm(ns, axis=1)[:, None]
                    intensity = .28 + .72 * np.maximum(0, ns @ light)
                    base = np.asarray((161, 178, 195) if housing else (69, 170, 191))
                    colors = np.clip(intensity[:, None] * base, 0, 255).astype(np.uint8)
                    panels[0].append((projected, colors))
                    if not housing:
                        panels[1].append((projected, colors))
            api["skeleton_graph"](api["join_stream"](data, entry), visit)
            manifests.append(info)
    image = Image.new("RGB", (1800, 1000), "#101925")
    draw = ImageDraw.Draw(image)
    font_path = Path("C:/Windows/Fonts/segoeui.ttf")
    font = lambda size: ImageFont.truetype(str(font_path), size) if font_path.exists() else ImageFont.load_default()
    draw.text((45, 25), "3DXML: complete decoded mesh assembly", font=font(32), fill="#edf4fc")
    draw.text((45, 75), "Stored triangle connectivity + XML occurrence transforms | Derived face normals", font=font(20), fill="#a9bed2")
    summary = {"scope": "visible XML occurrences; all decoded Skeleton mesh leaves",
               "renderer": "orthographic painter ordering; illustrative, not depth-buffer QA",
               "members": manifests, "panels": []}
    for panel, chunks in enumerate(panels):
        triangles = np.concatenate([v for v, c in chunks])
        colors = np.concatenate([c for v, c in chunks])
        del chunks
        lo = triangles[:, :, :2].min(axis=(0, 1))
        hi = triangles[:, :, :2].max(axis=(0, 1))
        zoom = min(800 / (hi[0] - lo[0]), 690 / (hi[1] - lo[1]))
        center = (lo + hi) / 2
        xy = (triangles[:, :, :2] - center) * (zoom, -zoom) + (450 + panel * 900, 505)
        order = np.argsort(triangles[:, :, 2].mean(axis=1))
        for i in order:
            draw.polygon([tuple(p) for p in xy[i]], fill=tuple(int(v) for v in colors[i]))
        title = "All visible geometry" if panel == 0 else "Same assembly, three housings hidden"
        draw.text((45 + panel * 900, 875), title, font=font(25), fill="#edf4fc")
        draw.text((45 + panel * 900, 925), "All meshes decoded; back faces culled for this view", font=font(18), fill="#a9bed2")
        summary["panels"].append({"title": title, "front_facing_triangles_drawn": len(order)})
        print(title, len(order), "triangles drawn", flush=True)
    image.save(args.output_directory / "complete_assembly.png")
    (args.output_directory / "assembly_preview_manifest.json").write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
