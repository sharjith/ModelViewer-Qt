# Overlay line weights and section hatching (3 November 2026)

Status: implemented, awaiting owner build and visual test. Nothing here is verified against Solid Edge; no Solid Edge capture has been compared.

## Observations (this repository)

- Before this change, dimension, extension and leader lines, arcs, in-plane text strokes, annotation strokes and recorded JT PMI strokes were all drawn at a fixed 2.0 px (`drawMeasurementOverlay`, `drawAnnotationOverlay`). Model feature/mesh edges were 1.0 to 1.75 px. The overlay was therefore heavier than the object lines it annotates.
- Section-capping hatch is the procedural shader `shaders/clipping_plane.frag`: spacing = local capping diagonal / tiling (world units), thickness = a fraction of that spacing. Both scale with zoom, unlike drafting hatch.

## Drafting convention (secondary sources)

- ISO 128 and ASME Y14.2 use two weights, thick:thin about 2:1. Object (visible outline) lines are thick; dimension, extension, projection, leader and hatch lines are thin. Sources: <https://engineeringtechnology.org/?p=1482>, <https://webstore.ansi.org/standards/asme/asmey142014r2020>. Quoted widths: 0.7/0.5/0.35 mm thick by sheet size (ISO-based), about 0.6 mm thick and 0.3 mm thin (ASME); ASME treats approximate widths as non-rejection.
- Pencil grades (object lines H to HB, dimension/extension/leader/centre lines about 2H to 4H, construction 4H to 6H) are the author's recollection of drafting practice, not source-verified.
- Section lining: straight, equally spaced, parallel lines (ASME Y14.2 definition as quoted at <https://eng-tips.com/threads/asme-y14-2m-y14-3m-section-lines.93530>), conventionally 45 degrees. Spacing values (about 1.5 to 3 mm, scaled to the area size, ISO minimum about 0.7 mm) are recollection and were not confirmed by the searches; check ISO 128-50 / ASME Y14.2 before relying on them.

## Owner decision, implemented

Object lines 1.5 px and thin lines 1.25 px, to begin with (`include/Measurement/OverlayLineWeights.h`). Thin: measurement and annotation overlay lines and strokes, and section hatching (new `hatchWidthPx` uniform, which overrides the fractional thickness). Object: mesh-edge and feature-edge passes (1.75 px feature edges lowered to 1.5). Hollow-mesh wireframe (1.25 px) and selection/gizmo outlines are unchanged.

## Not done

- Hatch spacing stays the existing tiling-based world-space value; the Settings "hatch thickness" value is now overridden by the pixel width.
- No device-pixel-ratio scaling; widths are device pixels.
