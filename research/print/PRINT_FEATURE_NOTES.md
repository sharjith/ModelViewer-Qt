# Print (File > Print...)

Branch `feature/print`, started 2026-11-10 from `dev` (21d5f42). Owner decisions of that day are recorded as such; everything else is
description of what the code does.

## Scope (owner decisions)

- The page is the view on a white background inside a sheet template (the templates Save as 3D PDF uses), or on a plain page.
- The view is the current one as shown, rendered on white.
- Print to PDF is the printer driver's (for example Microsoft Print to PDF); the application has no 2D PDF writer of its own for this.
- Qt6::PrintSupport is added to the build on this branch.

## How it works

1. `PrintDialog` (File > Print...) asks for the sheet template, the title block fields and a resolution (150, 300 or 600 dpi).
2. The system print dialog (`QPrintDialog`) chooses the printer and may change the paper. The paper starts as the template's page.
3. The viewport's background colours are set to white for one `captureCleanRayTracedImage()` grab (no axis triad or HUD) and put back.
4. `PrintPage::render()` draws the page as a raster image: the sheet frame through the shared sheet renderer (`SheetRenderer.h`) and the
   view inside the 3D area, centred and scaled to fit. The sheet renderer measures text in points and needs a 72 dpi paint device, so the
   image carries 72 dpi and the painter scales it by dpi/72.
5. The image is drawn onto the printer's paintable area.

## Known limits of this first version

- The view is the viewport's framebuffer, so its resolution is that of the window (device pixels); a large sheet printed at 300 dpi
  shows the view scaled up. A tiled high-resolution render is the follow-up if that is not sharp enough.
- Text on the sheet is raster, not vector.
- Template custom fields (SheetField) are not asked for, so they print blank; the standard title block fields are.
- With the path tracer active, setting the background restarts the accumulation, so the grab may be an unconverged frame.
- On Linux builds from vcpkg, real printers need Qt built with CUPS (feature `cups` of qtbase); the apt Qt6 builds have it.

## Checks to run (owner)

- File > Print... on a model: the dialog, then the system print dialog; print to a PDF printer and open the result.
- Default A4 template, A3 template, a user template, and "no template".
- Portrait and landscape views; the document must not be marked modified by printing (the background is restored).
- The File tab shows the Print button; Ctrl+P works; the action is disabled with no geometry.
