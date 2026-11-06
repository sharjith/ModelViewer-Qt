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

- Translations for the new *Levels* label and the Auto/Radial labels and tooltip: see the translations commit.
- Leaf groups that are one physical part made of several meshes (JT parts with several shapes) still explode apart, as in the flat
  layout. A rule for "treat a group of parts only as rigid" is undecided.
- Scene-structure changes that do not change bounds do not invalidate the cached hints (unchanged from before).

## Verification

Unit test `tests/exploded_view_tests.cpp` (target `exploded_view_tests`, `-DMV_BUILD_TESTS=ON`): collapse rules, `fromBoxes`
coverage/fan-out/cluster separation, local explosion of sub-assemblies, depth limit, anchor, linearity, determinism, and that a
one-group tree equals the flat Auto layout. Not run.

## Proportional layout (5 November 2026)

Owner feedback on a 4475-part JT vehicle model: the block-packing layout (flat Auto algorithm applied per level) gave lopsided gaps and rows of thin plates, where the reference viewer shows a symmetrical, proportionally weighted spread (observation from screenshots; the reference's algorithm is not documented).

The hierarchical layout now scales each group about its own centre: every item (part or sub-group block) moves by `(item centre - group centre) * gain`, where gain is 1.0 at the top level (factor 1) and is damped by 0.85 per level. Shape, symmetry and weights are kept; concentric parts stay concentric (overlaps are not resolved). The anchor item holds still. Sub-groups still enter the parent as rigid blocks, sized by their compact box plus 40% of their own growth. `MODELVIEWER_EXPLODED_PACK=1` selects the earlier packing layout for comparison; `MODELVIEWER_EXPLODED_FLAT=1` still forces the flat (non-hierarchical) layout. The constants live in `ExplodedViewManager.cpp` (`kScaleGain`, `kLevelDamping`, `kGroupGrowthShare`) and are untuned guesses.

## Levels, concentric parts and the contact-based Auto mode (5 November 2026)

**Levels.** The Levels combo box (All, 1..N) is rebuilt from the hierarchy depth of the selected strategy (`ExplodedGroupTree::levelCount`, filled in `ViewportWidget::updateExplosion`). Owner observation on a skateboard assembly: levels 2 and 3 looked identical. Cause (from the layout, confirmed by the fix): the wheel and the tire share a centre, so scaling about the group centre moved neither.

**Concentric parts.** `layoutRadialScaled` now spreads a group whose movable items all sit near its centre (at most four, within 20% of the group diagonal) along the group's thinnest axis. A first version applied this to any group and threw parts far away in the 126-part engine; it is restricted to fully concentric groups.

**Auto and Radial.** The combo now reads Auto (default), Radial, Axis X/Y/Z, Custom Vector. `Mode::AutoMating` is shown as *Auto*; `Mode::Auto` as *Radial*. Presets store the enum names (`AutoMating`, `Auto`), so older files still load (Radial). Index of Custom Vector moved from 4 to 5.

Auto (`layoutMating`, `ExplodedViewManager.cpp`) places the items of a group largest-first. Each part looks for the already placed items its box touches (gap within 5% of the group diagonal) and leaves along the axis of least box penetration, away from the partner; a rod (aspect 2:1) embedded in its partner leaves along its length; a part concentric with a similarly sized partner (centre within 10% of the larger extent, sizes within 1.5x) leaves along its thinnest axis, away from the assembly centre. The distance is the way out of the partner's box plus half the part's own size, added to the partner's own offset. A part keeps the radial placement when the rule is not trustworthy: no touching partner, a buried part that is neither a rod nor concentric, a best partner that does not beat the runner-up 2:1 unless all tied partners leave along the same axis (then the biggest partner gives the direction and the part goes beyond the one that moved furthest that way), or no direction on the axis.

Observations (owner screenshots and the `[ExplodedMating]` log, `MODELVIEWER_EXPLODED_DEBUG=1`):
- Skateboard (19 meshes): trucks drop straight down, screws leave downward, wheels leave their tires; owner accepted the result.
- Engine (126 meshes): the contact rule looked good with the first version; not rechecked after the later tightening.
- Bogie (62 meshes, one flat root group of 50 parts plus two wheel-shaft groups): most parts fail the trust test and fall back to radial, so Auto looks like Radial there. The owner judged Radial more realistic than the first mating version. Hypothesis, not tested further: box contact is unreliable for interleaved parts whose boxes overlap many neighbours. A group-size threshold was tried and dropped in favour of the per-part test.

Constants (`kPartnerDominance` 2.0, contact gap 5%, rod ratio 2.0, concentric 10% and 1.5x, clearance half the part size) are untuned guesses. Real contact detection from the meshes (faces, hole axes) was not attempted.

Tests added to `tests/exploded_view_tests.cpp` (truck below a board drops straight down while radial moves it sideways; wheel and tire separate along the thin axis in Auto and in Radial). Not run.
