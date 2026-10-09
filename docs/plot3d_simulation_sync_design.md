# Simulation result + plot played together - design

Why: which real studies need a Simulation result and a plot on one clock, how they should be laid out, and how it is staged. Written
before the code, because the first idea ("match both by progress, 0-100 %") turned out wrong for real data.

## Use cases

| # | Study | Same coordinates? | What is synchronised | Layout |
|---|---|---|---|---|
| 1 | Transient CFD / thermal result plus pathlines (or streaklines) of the same flow, e.g. a CSV time series exported from the solver | Yes | Physical time: result time = pathline time | One view, overlaid (no scale problem: both are in model coordinates) |
| 2 | Time history next to the animating field: max stress vs time, a node's displacement, a reaction force, measured test data | No (curve space vs model space) | Physical time, with a moving cursor on the curve | Split: model beside the chart |
| 3 | Modal analysis: mode shapes and a frequency-response curve | No | Frequency: the cursor sits at the current mode's frequency; clicking a peak selects the mode | Split |
| 4 | Load-step / parametric sweep: result per step, response vs load | No | The step's value against the chart's axis | Split |

## Principles

- **Match by physical value, not by progress.** Result 0..2 s with pathlines 0..1.5 s must line up at 1 s. By progress is only the
  fallback when the units are incompatible (a modal result is in Hz, pathlines in seconds) and the bar says so.
- **Overlay when the data shares coordinates, split when it does not.** The scale problem only exists in the second case.
- **Reuse what exists.** The Simulation module already has XY charts (`SimulationCharts`: plot over line, plot over time = a point's
  history over all steps) and a `SimulationChartWidget`; Compare mode already renders up to four panes, each with its own camera.

## Stages (each merged before the next)

1. **Clock + overlay** (`PlaybackClock`): one bar for a result and an animated pathline plot, matched by time (union of the two ranges,
   an item holds its first/last frame outside its own range; the result shows its last step at or before the clock time) or by progress.
   The combo's "All together" entry selects it. Sample: `pathlines_openfoam_cavity.csv` with the OpenFOAM cavity result.
2. **Time cursor + seek on the charts**: the chart of a point's history (and a modal result's frequency axis) shows a cursor at the current
   clock value and clicking / dragging it moves the clock; extra curves (test data, an FRF) can be added from a CSV.
3. **Plot panes in the split view** (only where a 3D plot really needs it): generalise Compare's panes to hold a Plot3D plot (its axes box,
   legend, notes and pathline heads drawn inside the pane, its own camera).
4. **Probe to history**: right-click a node on a result to put its value over all steps into the chart / a plot.

Stages 2-4 were re-scoped after finding the existing chart widget; they are revisited when stage 1 is in.
