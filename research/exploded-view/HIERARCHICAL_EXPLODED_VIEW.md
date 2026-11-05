# Hierarchical exploded view

Status: implemented 2026-11-05 including the Levels control. `exploded_view_tests` builds and passes; the full app build was run on the owner's authorisation (result in the commit). Not yet tested on a production model.

## Problem

Both Auto strategies laid out all parts in one flat pass (`ExplodedViewManager::recompute`). The scene tree was only used for
tie-breaking hints (`partGroupUuid` was filled but unused), so the parts of one sub-assembly flew apart independently. The pair
hints were computed over all parts, O(n^2) (about 6.6 million pair classifications for 3,625 meshes; not measured).

## Design

- Parts are arranged in a group tree (`ExplodedGroupTree`). Each group's parts and sub-groups are placed as rigid blocks around the
  group's own centre with the existing sector placement (`ExplodedViewManager::layoutFlat`, the former body of `recompute`).
- Layout runs bottom-up: a sub-group enters its parent as a block whose box already includes its own explosion. A part's offset is
  the sum of the offsets of the blocks above it. The anchor part stays put; at each level the block holding it is the anchor.
- Offsets stay linear in the factor, so slider ticks and preview steps remain `setFactor` only.
- Assembly-aware: tree = scene tree restricted to the selected parts. Wrapper nodes (no parts and one child), empty nodes and
  single-part nodes are collapsed (`fromDraft`). Pair hints are computed among siblings only
  (`buildSiblingPlacementHints`, groups above 300 parts get none).
- Classic: tree = spatial hierarchy over the part boxes (`fromBoxes`): split at the largest centre gap in the middle half of the
  longest axis, two binary splits per level (up to four children), leaf size 8. The existing runtime visibility BVH was not used: it
  mirrors the scene tree (`buildRuntimeVisibilityNodeRecursive` walks `SceneNode`), so it would equal Assembly-aware.
- Only Mode::Auto is hierarchical; the axis and vector modes are unchanged. `MODELVIEWER_EXPLODED_FLAT=1` restores the flat layout.
- Depth: the panel's *Levels* spin box (next to Strategy; 0 = All) limits how many levels from the root explode; deeper groups move as rigid blocks. It is stored in the preset (`explodeDepth`), is part of the layout cache key, and changing it relayouts (a group's size depends on how deep its inside explodes).

## Not done / open

- Translations for the new *Levels* label and tooltip (lupdate not run).
- Leaf groups that are one physical part made of several meshes (JT parts with several shapes) still explode apart, as in the flat
  layout. A rule for "treat a group of parts only as rigid" is undecided.
- Scene-structure changes that do not change bounds do not invalidate the cached hints (unchanged from before).

## Verification

Unit test `tests/exploded_view_tests.cpp` (target `exploded_view_tests`, `-DMV_BUILD_TESTS=ON`): collapse rules, `fromBoxes`
coverage/fan-out/cluster separation, local explosion of sub-assemblies, depth limit, anchor, linearity, determinism, and that a
one-group tree equals the flat Auto layout. Not run.
