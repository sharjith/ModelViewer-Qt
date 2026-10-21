# Repository working rules

Owner instructions recorded on 21 October 2026. Read [RESEARCH_FOLDER_HANDOUT.md](RESEARCH_FOLDER_HANDOUT.md) before further importer research or implementation; it contains the detailed research, provenance and repository rules.

- The owner normally builds and tests; do not run build/test commands without explicit owner authorization. On 21 October 2026 the owner authorized the local C2a control build/generation only; this does not authorize building the full viewer. Keep all generated controls/build output under ignored `out/`. Read-only analysis scripts on local samples are allowed.
- Stop at agreed build/test gates for the owner to confirm results before dependent implementation proceeds. Do not infer a test pass from a request to continue.
- Put feature research write-ups and validation records under `research/<topic>/`, as Markdown documents only. Keep reusable tooling under `tools/<topic>/` and regression tests/fixture generators under `tests/<topic>/`.
- Keep samples, sample-derived exports/payloads/images, generated fixtures, measurements, logs, scratch scripts and build products under ignored `out/` or outside the repo. Never add them to Git. Vendor documents and third-party repository snapshots also remain local.
- Use neutral sample IDs and hashes in tracked research. Do not publish private/downloaded model names or local sample paths. Record source URLs, versions/hashes, method and qualification limits; distinguish observations, hypotheses and owner acceptance. Preserve third-party notices and provenance manifests.
- Give exact commands for regenerating cited evidence, with output under `out/`. The owner runs build/test commands. Use relative document links and repair references when moving documents with `git mv`.
- Do not commit before the owner has tested and confirmed unless explicitly asked. Do not push unless asked. Preserve unrelated changes.
- Keep 3DXML-specific changes on both `feature/3dxml-importer` and `feature/JT-importer`, with separate commits and unrelated changes kept separate. Tell the owner before altering a branch checked out in another worktree; leave no stale worktrees.
- If required sample/reference evidence is missing, document the gap and ask. Do not fabricate or reheader files as conformance evidence.
