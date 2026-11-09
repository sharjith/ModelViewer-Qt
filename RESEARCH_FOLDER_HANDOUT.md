# Handout: where research material lives (for Codex)

Follow this for all further research on importers and other feature work. It records the owner's decisions of 21 October 2026.

## 1. The rule in one paragraph

Tracked research is **written documents only**, under `research/`. Everything else produced while researching (samples, exports, logs, JSON measurements, build products, scratch scripts) stays in the untracked `out/` folder and is regenerated from tracked tools when needed. Do not add data, samples or machine output to git, however small it seems.

## 2. Where each kind of artifact goes

| Artifact | Destination | Tracked? |
|---|---|---|
| Research write-up for a feature (format findings, decoding specs, validation records, feasibility, licensing and provenance statements, handouts) | `research/<topic>/<TOPIC>_<SUBJECT>.md` | **Yes** |
| Per-topic index and reading order | `research/<topic>/<TOPIC>_RESEARCH_INDEX.md` | Yes |
| Reusable analysis or generator tooling | `tools/<topic>/` (existing: `tools/3dxml`) | Yes, as code |
| Regression tests and fixture generators | `tests/<topic>/` (existing: `tests/jt`) | Yes, as code |
| Measurement output (JSON audits, inventories, validation reports) | `out/<topic>-research/NAME.json` | **No** |
| Logs, build folders, executables, objects | `out/...` | No |
| Samples and anything derived from them (`.jt`, `.3dxml`, `.3DRep`, stream dumps, OBJ/GLB exports, extracted payloads, preview images of third-party geometry, generated fixtures) | `out/...` or a folder outside the repo | **Never** |
| Third-party material (vendor PDFs, repo snapshots, READMEs copied from other projects) | local only; cite by URL, version and hash | **Never** |
| Compliance records that are not feature research (for example `docs/IMPORTER_NOTICE_VALIDATION.md`) | `docs/` | Yes |
| Website and user guide | `docs/` | Yes |
| Routine audits (tooltips, icons, key checks and similar) | not kept | No, unless the finding is genuinely novel; then write it up as a short document |

Current topics: `research/jt/`, `research/3dxml/`, `research/parasolid/`. Add a row to `research/README.md` when you create a new topic folder.

## 3. Why

- `out/` is volatile and ignored; nothing there survives a clean checkout.
- Sample redistribution rights are usually unknown. The NVIDIA VFI JT samples, for example, have no dataset-specific licence or author stated anywhere reachable, so they stay local and so does everything generated from them.
- Generated evidence is regenerable and bloats history (the 18 JSON files removed from `docs/` totalled about 2 MB).
- Documents are the part that carries lasting value: what was learned, how it was established, and where the limits are.

## 4. Writing the documents

1. Put a date and the branch at the top.
2. State provenance for the pass: what was consulted (with URL/version/hash), what was not, whether any vendor software, SDK, schema, disassembly or third-party decoder was used, and that the statement describes method, not legal clearance. When a later pass consults different sources, say so; do not let an early "bytes only" statement stand as a claim about the whole branch.
3. Refer to samples by neutral IDs (for example `L81`, `A103`) and hashes. Do not name downloaded or private files in tracked text, and do not publish local paths other than placeholders such as `<local-samples>/sample-A103.jt`.
4. Cite generated evidence as `out/<topic>-research/NAME.json` with the exact command that regenerates it (under `tools/` or `tests/`). Commands write to `out/`, never into `research/` or `docs/`.
5. Separate what is measured from what is hypothesis. Mark unverified claims, rejected assumptions and owner acceptance records with dates.
6. Quote third-party text only in short phrases with attribution. Never paste vendor specifications or schema tables.
7. Link siblings relatively. From `research/<topic>/`, the repo README is `../../README.md`; the notices are `../../THIRD_PARTY_NOTICES.md`.

## 5. Moving or renaming documents

Use `git mv` so history follows the file, then fix every reference:

- markdown links in other documents, `README.md`, `CREDITS.md`, `THIRD_PARTY_NOTICES.md`, `tests/*/README.md`;
- plain-text mentions of the old path;
- `src/ThirdParty/occt-jt/PROVENANCE.json` and `MODIFICATIONS.md`, which cite document paths (keep the JSON valid).

Quick link check for markdown (prints broken relative links; compare before and after):

```python
import posixpath, re, subprocess, os
files = [f for f in subprocess.check_output(['git','ls-files'], text=True).split('\n')
         if f.endswith('.md') and not f.startswith(('docs/guide','vcpkg/','sample-models','textures'))]
bad = []
for f in files:
    if not os.path.exists(f): continue
    for m in re.finditer(r'\]\(([^)\s]+)\)', open(f, encoding='utf-8', errors='replace').read()):
        t = m.group(1)
        if re.match(r'^[a-z][a-z0-9+.-]*:', t, re.I) or t.startswith('#'): continue
        p = posixpath.normpath(posixpath.join(posixpath.dirname(f), t.split('#')[0]))
        if t.split('#')[0] and not os.path.exists(p): bad.append(f'{f} -> {t}')
print(len(bad)); print('\n'.join(sorted(set(bad))))
```

The repository has roughly 110 unrelated broken links today (absolute-path links in `docs/mvf_format_spec.md`, template placeholders); only regressions matter.

Before finishing any research change, confirm:

- `git ls-files research | grep -v '\.md$'` prints nothing;
- no tracked file under `research/` or `docs/` references a sample by its real name;
- no new generated file is staged (`git status` shows only documents and code).

## 6. Provenance and notices

- Every third-party source consulted for an implementation is recorded in the topic's provenance document (`research/3dxml/3DXML_PROVENANCE.md` is the model) and, where code or a reference project is involved, in `CREDITS.md` or `THIRD_PARTY_NOTICES.md`.
- Vendored upstream code keeps its notices, dated modification notices and manifest entries (`src/ThirdParty/occt-jt/PROVENANCE.json`).
- New reading of a licence or terms (Siemens JT terms, NVIDIA terms, any vendor EULA) is summarised in the relevant research document, with the limits stated. Do not download vendor packages or accept terms on the owner's behalf.

## 7. Working agreements that still apply

- Never run build or test commands. The owner builds and tests. Read-only analysis scripts on local samples are fine.
- Do not commit before the owner has tested and confirmed, unless asked. Do not push unless asked.
- Keep 3DXML-specific changes on `feature/3dxml-importer` as well as `feature/JT-importer` (the JT branch was spun off from the 3DXML one). Use separate commits per branch and keep unrelated changes in separate commits.
- If a branch is checked out in another worktree, do not move its ref or commit into that worktree without telling the owner. Leave no stale worktrees behind.
- If a task would need a sample you do not have, record the gap in the document and ask; do not fabricate or reheader a file as evidence.

## 8. Current state

- `feature/JT-importer` (commit `6433da9`): 57 research documents moved into `research/` (`jt`, `3dxml`, `parasolid`), 18 JSON evidence files untracked, references rewritten, `research/README.md` added. `check_provenance.py` was not run (owner runs checks).
- `feature/3dxml-importer` (commit `927c482`): the matching 3DXML-only move (17 documents including the handout, 17 JSON files untracked, `research/README.md` listing only `3dxml/`).
