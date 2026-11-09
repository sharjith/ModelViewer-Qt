# Exploded view on large models (4 November 2026)

Prompted by the owner: on a very large assembly (about 4,500 meshes, 8.8 million triangles, so draw batching and the interaction fast path are active) moving the explosion factor and playing the explosion animation was "horribly slow", while orbiting had been made fast. Method: code reading only. There was no log or profile of an explosion run, so the causes below are read from the source and not measured; the numbers to compare are listed at the end.

## What one explosion tick costs

`ViewportWidget::updateExplosion()` runs for every slider change and every step of the panel's 16 ms preview timer.

1. **Layout recomputed every tick.** `ExplodedViewManager::recompute()` sorts parts, assigns sectors and places them on every call. The offsets are linear in the factor (the factor is only used in `applyFactor()`), so a factor-only tick needed no layout work at all.
2. **Two passes over every mesh.** One pass cleared each mesh's offset and bounds (so the unexploded boxes could be measured), a second applied the new offset. Each call invalidates the combined transform and bumps the render-state epoch.
3. **No fast path while it moves.** `interactionLowRes()` covered camera motion only. While the explosion changes, the meshes' transforms differ from the ones the draw batches were built with, so the batcher draws them one by one at full resolution, and shadows are recomputed.
4. The scene-graph world transforms are evaluated on every tick and the result is not used in the code path read. Left alone, in case of side effects.

## Changes (untested, not built)

- `ExplodedViewManager::setFactor()` and `hasLayout()`: rescale the stored offsets without recomputing the layout.
- `ViewportWidget::updateExplosion()`: a factor-only tick reuses the layout when the assembly set, anchor, mode, vector and strategy are unchanged and the runtime bounds revision still equals the value recorded right after the previous update (so any other change to a mesh's transform or bounds forces a full recompute). That path skips the layout and the clearing pass.
- `interactionLowRes()` is now also true for 250 ms after each explosion update (`_explosionMotionTimer`, which also schedules one full-quality frame when the explosion stops) and while the animation timer runs. Effects on a model above the interaction fast-path size: coarse LOD copies, small-part culling, single-sample rasterisation, no reflection pass. On every model: shadows are off while the explosion moves.

## Second round: the captured-step preview animation (5 November 2026)

Owner after the first build: the factor slider works, the animation is still slow. Reading: the preview player (`ExplodedViewPanel::onDraftPreviewTick` every 16 ms, then `applyDraftPreviewPose`) does not go through `updateExplosion()` at all, so the first round's fast path never applied to it. Per tick it also rewrote every mesh in the file (`setExplosionOffset` plus `setSceneRenderTransformFast`, each invalidating transform and bounds and bumping the render-state epoch), although only the meshes under animated nodes move.

- `applyDraftPreviewPose` now calls `ViewportWidget::noteExplodedViewPoseChanged()` so playback and scrubbing get the interaction fast path (coarse LODs, small-part culling, single sample, no shadows), with a full-quality frame 250 ms after the last pose.
- Ticks touch only meshes that are animated themselves or sit under an animated node. A full pass (every mesh) still runs for the first pose of a session and whenever the set of animated nodes and meshes differs from the previous tick (`_draftPreviewPoseSignature`), so a mesh that stops moving is reset.
- Not changed: `resolvedTopLevelCapturedEntries()` still deep-copies the step list a couple of times per tick; the scene-node walk (matrix per node) remains. Both are small next to the per-mesh work, but should show up in a profile if it is still slow.
- Still the real limit: batched drawing cannot draw moved meshes, so every moving mesh is drawn individually.

## Layout scale grew with the part count (5 November 2026)

Owner, from a screenshot of 4,475 meshes at 100 %: the assembly is spread over many times its own size, while GrabCAD's explosion of the same model stays compact. Cause, read from `ExplodedViewManager::recompute()` (Auto mode): each of the six sectors was packed as one chain, every part placed after the previous one along the axis with a gap of at least 5 % of the assembly radius. Spread therefore grew linearly with the number of parts in a sector (several hundred parts times 5 % of the radius is tens of radii). The axial modes had the same floor.

Change (untested): in Auto mode a part moves out by 60 % of its own distance from the origin (the assembly scales up about the origin), and is pushed further only past parts whose footprint overlaps its own across the axis and that were placed before it, with a gap of 30 % of its size (floor 0.4 % of the radius). Axial modes cap the radius-based gap floor so the gaps add up to at most about one radius. The backbone chains are unchanged. Parts that need no clearance no longer queue behind unrelated ones. The cost is a pairwise overlap test per sector (a few million box tests for this model, at layout time only, not per slider tick). Constants: `kSpread`, the gap factor and `minGap` in the sector loop. Captured steps and animations made with the old layout must be captured again.

Round 2 (owner: better, still long rails of parts along the X and Y axes and a dense knot in the middle): the clearance push chains, so deep stacks and parts under a large-footprint part queue up into rails. Added a cap: a part is pushed past others by at most 0.4 of the assembly radius (`maxPush`), and the clearance gap is 20 % of the part's size. Beyond the cap parts can overlap in depth. The real fix is hierarchical: explode sub-assemblies as units, then the parts inside each one (the relation hints and the assembly tree exist); not done.

## Animation still stutters (5 November 2026)

Not diagnosed. The changes above remove the per-mesh transform rewrites and apply the fast path, but every moving mesh is still drawn individually (4,475 draw calls). If it persists, `[RenderDiagnostics]` lines taken during playback are needed to see whether the time is CPU in the pass or in the tick.

## Not done

- Batched drawing still cannot draw moved meshes: a batch stores each part's transform baked in. A per-batch offset, or re-batching at the end of the move, would let an exploding assembly stay batched. This is the next step if the fast path alone is not enough.
- The layout itself (full path) is unchanged and runs when the assembly, anchor, mode or strategy changes, or after any other transform change.
- `evaluateWorldTransforms()` per tick (item 4).

## What to compare

With "Profile Rendering" on, the `[RenderDiagnostics]` lines in the log give frame times. Before and after, on the same model: drag the factor slider, and play the preview animation, and compare `frameCpuMs/frame` and `opaqueMs/frame`. Also check that the final frame after the slider stops is full quality, and that exploding by an anchor or a vector, and after moving a part by hand, still lays out correctly (the cache must not return stale positions).
