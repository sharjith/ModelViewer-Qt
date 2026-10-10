# UI test plan: a scripted in-app driver

Status: **PARKED** (2026-10-10, by the user). Plan only, nothing is built. Written for the 2026.10 Simulation / Plot3D work, but meant for the whole application. Do not start phase 0 until the user asks; the remarks in section 9 record the doubts raised when it was parked.

## 1. Why, and what it is for

The existing tests (`result_tests`, `plot3d_tests`, `mvf_tests`) are GUI-free: they check readers, builders, serialisation and maths, and they are good at it. What they cannot see is the **wiring**: a panel that does not reach the model, a signal that is connected only when a result is opened, a state that is saved but not restored, a dialog that says one thing and builds another. Recent bugs of exactly that kind, none of which a unit test could have caught:

| Bug found by hand | What a UI test would have checked |
|---|---|
| Note placement did nothing in a plot-only document (signals were connected only when a result opened) | open a CSV plot, arm "Add note", click in the view, expect one note |
| Edit Plot did not take the new Base Z on Rebuild | edit, change the spin, Rebuild, read the session |
| Image plane black after Preview, then after Build (shared texture cache) | build the plot, screenshot, expect a non-black picture |
| Status balloon black on black | show it, grab it, expect text pixels |
| Result squished after switching ADS to ray tracing | switch the mode, screenshot, compare |
| A panel row not saved in the `.mvf` | change, save, reopen, expect the same value |

The aim is therefore: **drive the real application the way a user does, check what the user would check, and fail loudly when it differs.** It is not a replacement for the unit tests and does not try to test pixel-perfect rendering across GPUs.

## 2. Options considered

| Option | For | Against |
|---|---|---|
| External tools (Squish, UiPath, FlaUI, Windows UIA) | no code in the product | the viewport is one OpenGL widget: an accessibility tree shows nothing inside it; Qt custom widgets expose little; licences (Squish) or per-OS drivers; scenarios cannot read the model |
| Qt Test (`QTest::mouseClick`, `QTest::keyClicks`) in a separate executable | standard, no new tool | has to construct the main window itself, so it needs the whole application linked into the test; no scenario files (every scenario is C++); slow to write |
| **In-app driver** (`ModelViewer --ui-test scenario.json`) | uses the real window, real GL context, real dialogs; scenarios are small JSON files; can read model state directly; works the same on Windows and Linux; no licence; the assistant can write scenarios without building | adds a small amount of test-only code to the product (inert unless the flag is given) |

**Recommendation: the in-app driver**, with Qt Test kept as an optional second layer for dialogs that can be built in isolation (e.g. `Plot3DPanel`) if we ever want it.

## 3. The driver

### 3.1 Shape

- A class `UiTestRunner` (in `src/App/`), created in `main.cpp` only when `--ui-test <file>` is on the command line, after the main window is up. Without the flag it does not exist.
- It reads a scenario (JSON), runs the steps one by one **on the GUI thread through the event loop** (so nothing is faked: signals, timers and queued connections run as for a user), and writes a report.
- Options: `--ui-test <scenario.json | folder>`, `--ui-test-out <dir>` (report, screenshots, log), `--ui-test-update-baselines`, `--ui-test-timeout <s>` (whole run), `--ui-test-lang <en|de|...>`.
- Exit code 0 = all passed, 1 = a check failed, 2 = driver error (bad scenario, timeout). A report file lists each step with its result and, for a failure, the expected and the actual value.

### 3.2 Scenario file

```json
{
  "name": "plot3d_curve_fill_edit",
  "window": { "width": 1600, "height": 900 },
  "steps": [
    { "do": "open",   "file": "{samples}/Plot3D/line_helix.csv" },
    { "do": "menu",   "path": "3D Plot > Add 3D Plot..." },
    { "do": "select", "widget": "Plot3DPanel/primitive", "text": "Line / Curve" },
    { "do": "check",  "widget": "Plot3DPanel/scatterFill", "checked": true },
    { "do": "click",  "widget": "Plot3DPanel/build" },
    { "do": "expect", "probe": "plot3d.count", "equals": 1 },
    { "do": "screenshot", "name": "filled_helix", "compare": "filled_helix.png", "tolerance": 0.02 }
  ]
}
```

Step vocabulary (small on purpose): `open` / `saveAs` / `reopen` (file operations without native dialogs), `menu`, `click`, `doubleClick`, `rightClick` (widget or viewport point), `set` (spin / edit text), `select` (combo by text or data), `check`, `key`, `wait` (a condition or a number of frames, never a bare sleep), `expect` (a widget property, a probe, a log line), `screenshot`, `dialog` (see 3.5), `call` (a named high-level command, for things not worth clicking, e.g. "simulation.openResult").

Placeholders: `{samples}` (the shipped sample-models folder), `{tmp}` (a per-run temp folder), `{out}`.

### 3.3 Finding widgets

By `objectName`, written as a path: `Plot3DPanel/columnFillTo`. Today only about 33 widgets in the code-built panels have an `objectName` (the `.ui`-file ones are named by Qt Designer). The work:

1. A tiny helper macro `MV_NAME(member)` that sets `member->setObjectName(#member without the leading underscore)`, applied to the panels the scenarios touch, so naming is one line per widget and stable by construction.
2. The runner refuses an ambiguous or missing path with a clear error (it lists the closest names), so a rename breaks a scenario loudly instead of silently.

Start with: Simulation panel, Plot3D panel and controls, chart window, the main menus and toolbars, the status balloon.

### 3.4 Reading state: probes

A button's text is not the model. A small registry (`UiTestProbes`) lets the product expose a few named read-only values that scenarios check: `scene.meshCount`, `undo.count`, `simulation.fieldIndex`, `simulation.step`, `simulation.averageCellData`, `plot3d.count`, `plot3d.session[0].isFilled`, `viewport.renderMode`. Each probe is a lambda registered next to the code that owns the value (one line), compiled only in builds with the driver. This is what makes "save, reopen, same state" a one-line check.

### 3.5 Dialogs and file choosers

Modal `QMessageBox`es block an event loop-driven script. The runner installs a **dialog policy**: by default any message box found open is recorded (title and text become part of the report) and answered with its default button, and a scenario can state what it expects (`{"do":"dialog","expect":"title contains Chart","answer":"OK"}`). An unexpected dialog fails the step, which is how the "cannot be charted" message would have been noticed. Native file dialogs are never opened: `open`, `saveAs` and the CSV / image pickers have direct forms taking a path.

### 3.6 Screenshots

Captured with the viewport's `grabFramebuffer()` (the GL content) or `QWidget::grab()` (panels, chart window), at a fixed window size from the scenario. Two ways to judge a picture:

1. **Same-run checks (the default, no stored images):**
   - *not black / not uniform / has the expected colours* (count distinct colours, or check a region) - this is how the black image plane and the black balloon would have been caught;
   - *before versus after in one run* - two screenshots must differ (flat versus smooth cell data) or must match (ADS to ray tracing and back must leave the model where it was, the squish bug);
   - *geometry* - the model's bounding rectangle in the picture stays where it was.
2. **Baseline comparison (optional, later):** a stored PNG compared with a per-pixel tolerance and a fraction of differing pixels, with `actual` / `baseline` / `diff` images in the report and masks for HUD regions. GPUs and drivers differ in filtering, so baselines would be per machine class. `--ui-test-update-baselines` rewrites them after a deliberate visual change. Whether this is worth having, and where the images would live (in the repository or outside it), is open: see section 9.

### 3.7 Determinism

A run must not depend on the user's setup or leave traces:

- Settings isolated: the driver points `QSettings` at a temp INI (own organisation/application name) and starts from defaults, so recent files, window layout and the "restore last file" option cannot interfere; the user's real settings are never read or written.
- Fixed language (default English; one German scenario checks translated messages), splash off, a fixed window size, MSAA off, no animations, rendering mode set explicitly by the scenario.
- `wait` conditions poll with a timeout (e.g. "the result has finished loading", "N frames swapped"), never a fixed delay.
- Application warnings and errors written to the log during a scenario fail it unless the scenario lists them as expected.

## 4. Scenario library, version 1

Each maps to something shipped in this release. Pass criteria are structural first.

| # | Scenario | Checks |
|---|---|---|
| S1 | Open `FEM_box_static.frd`, change field, component, colour map, range | probes `fieldIndex`, legend range changes; screenshot is coloured (not grey) |
| S2 | Save the session as `.mvf`, reopen | every view-state probe equal before and after (field, step, deform, averageCellData, tensor field/count) |
| S3 | `cell_data_cube.vtk`: toggle "Average cell data to nodes" | probe flips; screenshot differs flat vs smooth |
| S4 | Plot over time / over line on a node, a cell and a shell result (`shell_plate_transient.frd`) | a chart window appears with the expected sample count; CSV export reads back; no "cannot be charted" dialog |
| S5 | `stress_states.vtu`: stress ellipsoids, switch Tensor field, change count | probe `tensorGlyph.count` follows the spin; field list has three entries; session round-trip |
| S6 | Plot3D scatter fill-between (`scatter_band.csv`) | build; Edit Plot; change the column; Rebuild; bounds include the second set |
| S7 | Plot3D parametric curve fill (down to Base Z, then up to z2(t)) | build, Edit Plot, change Base Z / z2, Rebuild; session saved and reopened |
| S8 | Image plane: opacity, "Readable from behind", alpha picture | probe `plot3d.session[0].imageOpacity`; screenshot not black; back-readable box disabled below 100 % |
| S9 | Text notes: arm Add note, click in the view, drag, edit, undo, redo | note count after each step; undo stack count |
| S10 | German UI: provoke a formula error | the dialog text equals the German translation |
| S11 | ADS to ray tracing and back with a result open | screenshot geometry unchanged (the squish bug) |
| S12 | Synchronised playback: result and plot together | step probe follows the clock; chart cursor moves |

Further scenarios (materials, measurement, mesh tools, import/export round trips) can be added the same way later, in priority order the user chooses.

## 5. Phasing

| Phase | Content | Deliverable |
|---|---|---|
| 0, foundation | `UiTestRunner` skeleton, CLI flags, settings isolation, report, `MV_NAME` on the Simulation and Plot3D panels, steps `open` `click` `set` `select` `check` `expect` `wait` | S1 and S9 running |
| 1, state and dialogs | probes, dialog policy, log checks, `saveAs` / `reopen` | S2 to S5 |
| 2, pictures | screenshots with baselines, diff images, masks, update flag | S3, S8, S11 with images |
| 3, library | the remaining scenarios, German run, `run_ui_tests.ps1` (runs a folder, collects reports, one summary) | full v1 library |
| 4, optional | a "record" helper that writes a scenario from a manual session; JUnit XML for a CI server | convenience |

Each phase is its own branch, merged when it works, like the rest of this release.

## 6. Who does what

- The assistant writes the driver, the `objectName` / probe additions, the scenarios and the scripts, and never builds or runs them (the build lock stays).
- The user builds, runs `ModelViewer --ui-test <folder>` (or the script) and sends the report folder back; failures come with step, expected and actual value, and screenshots, so a failing run can be fixed without a live session.
- Anything the driver cannot see (native dialogs, drag from the OS) stays a manual test, listed in the README's test recipes as now.

## 7. Risks and how they are handled

| Risk | Handling |
|---|---|
| Flaky timing (async loading, GL) | conditions with timeouts, frame-swap waits, never sleeps; a step retries its lookup briefly |
| GPU differences make screenshots unstable | structure-first checks, per-machine baselines, tolerance and masks, screenshots only where visual |
| Test code in the product | one runner class and a few probe registrations, inert without the flag; can be excluded by a CMake option (`MV_UI_TESTS`) |
| Renamed widgets break scenarios | stable `objectName` by `MV_NAME`; clear error with near matches |
| Scenarios rot | they live in the repo, run by one script, and the README says which feature each covers |
| A modal dialog hangs a run | dialog policy plus a whole-run timeout that writes the report so far |

## 8. Decisions

Decided with the user before parking:

1. **Build option:** the driver is compiled only with the CMake option `MV_UI_TESTS` (ON in the development presets, OFF for release packages).
2. **First scenarios (phase 0), when resumed:** S1 (Simulation: open a result, change field / component / colour map / range) and S9 (Plot3D text notes: place, drag, edit, undo, redo).
3. **Visible window:** the first version drives the normal visible window; a hidden/offscreen run is a later experiment.

Open: screenshot baselines (see 9), and anything to add to or drop from the v1 library (section 4).

## 9. Remarks when the plan was parked

- A first answer put the baselines in the repository (`tests/ui/baselines/<gpu-tag>/`). On reflection this was **reopened**: it means baselining many images, every deliberate UI change (a colour map, a theme, a layout) would invalidate some of them, and the set is per GPU. Whether baselines belong in the repository at all is undecided.
- The plan does not depend on them: most scenarios check structure (probes, widget values) and use same-run picture checks (3.6). If baselines are ever added, keep them few (S3, S8, S11 at most), optional (a missing baseline reports "no baseline yet", it does not fail), and preferably outside the repository.
- The whole activity is parked: no code was written, nothing in the product changed.
