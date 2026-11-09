# On-demand assembly loading (3DXML and JT)

Status: design decision recorded 2026-10-27. Not implemented. Owner decision, not an observation.

## Origin

A demand-driven loading model was described in the JT research notes (`research/jt/JT_VIEWING_PRIORITY.md`): the product
structure and metadata load first, and a part's geometry loads when its tree item is checked. Where that model fetches parts from
a managed repository, this viewer reads them from local disk.

## Owner decisions

1. On-demand loading is a user setting.
2. The setting is overridden for very large assemblies: even when it is off, an assembly above a size threshold shows a warning
   and loads in on-demand mode automatically.
3. The settings live in the settings dialog (Import Export tab, "3DXML and JT" group), stored in `QSettings`, replacing the
   environment variables `MODELVIEWER_3DXML_STRICT` and `MODELVIEWER_JT_BUDGET_GIB` as the user-facing route.

## Design points to settle before implementation

- **Qualifying inputs.** Assemblies whose parts are separate files (JT external references, 3DXML representation members and
  external documents). A monolithic file qualifies only where its internal segments can be read independently; otherwise the
  warning is shown and the file loads fully.
- **Threshold.** Not chosen. It should count parts, referenced file bytes or an estimated decoded size; measure on the
  130-representation production assembly before fixing a number. Report the threshold in the warning text.
- **State per node.** Not loaded, loading, loaded, failed. Checked means requested and visible. Unchecking hides; unloading and
  cache eviction are separate (cache size limit is a setting).
- **Sharing.** Instances of one part share one loaded geometry.
- **Failure handling.** Per-part progress, cancel and retry; a failed part is listed in the import warnings and does not fail the
  assembly (same rule as the skip-and-list behaviour for unreadable 3DXML representations).
- **Provider interface.** Part retrieval sits behind an interface so a local-disk provider can be replaced by another source.

## Settings implied

| Setting | Notes |
|---|---|
| Load assemblies on demand | off / on |
| Large-assembly threshold | warns and forces on-demand above it |
| Parts loaded concurrently | bounded worker count |
| Part cache limit | eviction is separate from visibility |

## Open questions

- Whether the default is off with the automatic large-assembly override, as decided, or on for all multi-file assemblies.
- Whether unloading a part removes it from undo history.
- How selection, measurement and section capping treat not-loaded parts.
