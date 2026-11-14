# Literature notes: depth range, precision and navigation in professional 3D viewers

Companion to [zoom-and-depth-range.md](zoom-and-depth-range.md). Written 2026-11-14 from web searches.

Qualification limits: what follows is taken from search-result summaries and the excerpts the search tool
returned, not from reading the full papers. Entries marked "not verified" were not confirmed in a primary source.
Statements about this viewer are my own inference, not findings from these sources.

## 1. Camera-relative rendering (relative to eye / relative to center)

- Ohlarik, AGI, 2008 ("Precisions, Precisions"): GPUs work in 32-bit float; the fix used in AGI's products is to render each
  vertex relative to the viewer, so the model-view matrix holds only a rotation and the translation is applied on the CPU in
  double precision. The matrix is rebuilt when the viewer moves, which costs little against rendering the model.
  [AGI blog](https://help.agi.com/AGIComponents/html/BlogPrecisionsPrecisions.htm)
- Cesium's `CESIUM_RTC` glTF extension carries a per-model centre so the large translation is never subtracted in float on the GPU.
  [CESIUM_RTC](https://kcoley.github.io/glTF/extensions/1.0/Vendor/CESIUM_RTC)
- Speckle (BIM viewer, models from 1 mm to 100 km at arbitrary offsets): first relative-to-centre, then relative-to-eye with
  vertex and camera positions held as doubles and split into two floats each; the subtraction happens in the shader. They note
  it works only up to certain distances and reported platform-specific problems.
  [Speckle blog](https://speckle.systems/blog/improving-speckles-rte-implementation),
  [spatial jitter](https://v1.speckle.systems/blog/speckles-take-on-spatial-jitter-2)
- Orillusion's guide makes the point that if vertex data are stored as absolute large coordinates, precision is already lost when
  the data is built, so the technique must be paired with double-precision matrices on the CPU and small, local vertex data.
  [Orillusion](https://www.orillusion.com/en/guide/advanced/rte.html)
- Kerbal Space Program's planet tech generates mesh positions relative to a floating origin.
  [KSP dev diary](https://www.kerbalspaceprogram.com:443/news/dev-diaries-developer-insights-12-planet-tech)

Relevance here: this is the "local origin" option (part C). It only helps when vertex data are also local to a small origin
(per-mesh or per-scene offset); otherwise the loss has already happened at import.

## 2. Depth buffer precision

- Reed, "Depth Precision Visualized" (2015), and NVIDIA's reprint: a standard 1/z depth mapping puts most precision near the
  near plane; float depth is densest near zero, so mapping near to 1 and far to 0 (reversed-Z) with a floating-point depth
  buffer spreads precision far more evenly, and an infinite far plane costs almost nothing. In OpenGL this needs
  `glClipControl` (zero-to-one depth), a float depth buffer and a greater-than depth test.
  [Reed](https://www.reedbeta.com/blog/depth-precision-visualized/),
  [NVIDIA](https://developer.nvidia.com/blog/visualizing-depth-precision/),
  [analysis](https://zero-radiance.github.io/post/z-buffer)
- Outerra: logarithmic and reversed floating-point depth buffers for planetary scale; they also report float depth alone was not
  enough there.
  [Outerra log z](https://outerra.blogspot.com/2009/08/logarithmic-z-buffer.html),
  [Outerra float depth](https://outerra.blogspot.com/2009/12/floating-point-depth-buffer.html)
- Cesium: multi-frustum rendering (several depth ranges drawn in turn), later a hybrid with a logarithmic depth buffer inside each
  frustum, with fewer draw calls and a nearer near plane.
  [Cesium blog](https://cesium.com/blog/2018/05/24/logarithmic-depth/)

Relevance here: these target planet-scale ranges. For CAD-scale zoom (ratios around 1e4 to 1e6) a tight near/far fit gets most of
the benefit; reversed-Z with a float depth buffer is the stronger option if more is needed, but the window's framebuffer is
24-bit fixed (see the main note).

## 3. Fitting near and far to the scene

- VTK `vtkRenderer::ResetCameraClippingRange` sets the range from the bounds of the visible props. It limits how close near may
  get to far according to the depth buffer's bit depth (the source treats 16-bit buffers more conservatively than deeper ones;
  I saw tolerances of 0.01 and 0.001 quoted). A mailing-list report argues the perspective-only adjustments are unnecessary,
  and cause clipping, in parallel projection (not verified whether later releases changed this).
  [VTK renderer docs](https://epgmod.phys.tue.nl/vtk/9.1/html/classvtkRenderer.html),
  [vtkusers thread](https://public.kitware.com/pipermail/vtkusers/2009-April/051510.html)
- OpenSceneGraph computes near/far from bounding volumes by default, with an option to use the primitives themselves; a
  developer on the list says the primitive-based mode uses the depth range better.
  [OSG CullSettings](https://camlosg.sourceforge.net/osg/classosg_1_1CullSettings.html)
- Rhino users see geometry cut when zooming in because depth precision is non-linear; Rhino exposes no manual near/far.
  [McNeel forum](https://discourse.mcneel.com/t/rhino-cuts-objects-like-an-invisible-clipping-plane-when-zooming-in/216045)

Relevance here: part A is the same approach as VTK's (bounds of the visible meshes). OSG's two modes match the limit already
noted: bounding boxes keep the range wide when a large part spans the view, and the primitive-level range (in practice, the
depth actually drawn) is tighter.

## 4. Navigation: pivot, zoom and speed near surfaces

- Fitzmaurice, Matejka, Mordatch, Khan, Kurtenbach (Autodesk Research), "Safe 3D Navigation", I3D 2008: adds safeguards to the
  pan/zoom/orbit tools; they name the pivot being in front of, on or behind a surface and the distance to the surface as the core
  difficulties, show the pivot depending on its position relative to surfaces, and report integration into six commercial CAD
  applications.
  [Autodesk Research](https://www.research.autodesk.com/publications/safe-3d-navigation/),
  [PDF](https://research.autodesk.com/app/uploads/2023/03/safe-3d-navigation.pdf_recHpo5niNlpDtQQu.pdf)
- McCrae, Mordatch, Glueck, Khan, "Multiscale 3D Navigation", I3D 2009: a depth cubemap around the camera gives the distance to
  surrounding geometry in every direction; it drives scale-consistent navigation and collision without precomputation, and the
  related patent describes setting the viewing frustum from those distances. I did not confirm the exact near/far mapping.
  [PDF](https://www.dgp.toronto.edu/~mccrae/projects/multiscalenav/Multiscale3DNav.pdf),
  [patent US8665260](https://patents.google.com/patent/US8665260)
- Autodesk patents describe a zoom that moves in and out from the cursor position rather than the view centre.
  [US9021400](https://patents.google.com/patent/US9021400)
- Three.js CAD-camera discussions: the rotation pivot is kept separate from the view direction, resolved near the cursor on
  mouse-down, and kept near nearby geometry during drags.
  [CAD camera controls](https://discourse.threejs.org/t/cad-camera-controls/92168),
  [orbit pivot thread](https://discourse.threejs.org/t/cad-style-orbit-pivot-how-to-keep-the-rotation-center-close-to-nearby-objects-smoothly/91618)

Relevance here: this is part B (pivot on the surface under the cursor, speed and limits from the distance to the nearest
surface). The multiscale paper's depth-cubemap idea is the principled version of "limit zoom by distance to real geometry".

## Not found

No source found that spells out, end to end, how a commercial CAD or visualisation package (the owner mentioned VisMockup) sets
near/far, zoom limits and origin together; those are closed products. The sources above document the individual techniques only.
