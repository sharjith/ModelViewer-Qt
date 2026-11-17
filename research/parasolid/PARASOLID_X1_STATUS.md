# Parasolid X_T stage X1: status and gate G1

15 November 2026, branch `feature/parasolid-importer`. **Written, not built, not run.** The owner builds and tests (see `AGENTS.md`). No importer is wired into the application. Plan: [PARASOLID_IMPLEMENTATION_PLAN.md](PARASOLID_IMPLEMENTATION_PLAN.md), stage X1. Owner decisions that shaped it: proceed with development and resolve the permission question before release; base layouts are loaded from a local untracked file (15 Nov 2026).

## What exists (all uncommitted)

| File | Role |
|---|---|
| `include/Import/Parasolid/XtLayout.h`, `src/Import/Parasolid/XtLayout.cpp` | Field-layout types and a parser for the local layout text file. No layouts are stored in the repository. |
| `include/Import/Parasolid/XtReader.h`, `src/Import/Parasolid/XtReader.cpp` | Text X_T reader: header, prefix, embedded-schema edit scripts, in-stream node types, every record, unresolved-pointer count, limits, cancellation, typed diagnostics. Standard C++17, no Qt, no OCCT. |
| `tests/parasolid/xt_reader_tests.cpp`, `tests/parasolid/CMakeLists.txt` | Synthetic fixtures only (invented layouts): layout parser, base and edited records, unset values, variable-length nodes, in-stream types, refusals, limits, cancellation, truncation at every boundary. |
| `tests/parasolid/xt_decode_cli.cpp` | Inspector for local samples. Prints one JSON object per file in the same fields as `tools/parasolid/xt_decode.py`. |
| `tests/CMakeLists.txt` | One line added: `add_subdirectory(parasolid)`. |

The Python tool [`tools/parasolid/xt_decode.py`](../../tools/parasolid/xt_decode.py) is the reference the C++ must match. Since the first run it also checks that each edit script yields the declared field count; this held on all 12 files.

## Behaviour defined for X1

- Text transport only. X_B, unknown transports and **legacy unsuffixed schema keys are refused** with a typed diagnostic.
- A node type that uses the base layout but is missing from the local layout file stops the decode with `unknown-base-layout` (this is the type-151 case).
- Types the file defines itself are read generically and listed, so unknown newer types do not stop the decode.
- Limits on records, string length, variable-length counts and total values; cancellation polled every 4096 records; no exceptions escape for malformed input.

## Not verified

Nothing here has been compiled. I checked the C++ by reading it and by tracing every test fixture by hand against the reader, but compile errors or a wrong hand-trace are possible. The reference results are the Python decoder's: 8 of 9 embedded-schema samples decode to the terminator.

## Gate G1 (owner)

Run, then record the result in the table below.

1. Build and run `parasolid_xt_tests`. Expect: all checks pass.
2. Build `parasolid_xt_decode` and run it over the samples with the local layout file `out/parasolid-research/layouts-v13.txt`:

```powershell
parasolid_xt_decode out/parasolid-research/layouts-v13.txt "<local-samples>" > out/parasolid-research/decode-cpp.json
```

3. Compare with the Python results from `python -I tools/parasolid/xt_decode.py out/parasolid-research/layouts-v13.json "<local-samples>" > out/parasolid-research/decode.json`. Expect identical `status`, `records` and `unresolved_pointers` for every file: 8 complete, one stopped at type 151, three refused as legacy.

`layouts-v13.txt` is a local file derived from the local layout JSON (not tracked).

| Date | Build | `parasolid_xt_tests` | Sample comparison | Exceptions |
|---|---|---|---|---|
| | | | | |

## Update: neutral binary and the JT table (15 Nov 2026)

Added after the first status was written (all uncommitted, not built, not run):

| File | Role |
|---|---|
| `src/Import/Parasolid/XtReader.cpp` (rewritten) | One record decoder shared by a text source and a **neutral-binary source** (big-endian, IEEE doubles, the pointer-index encoding, 2-byte node types, 4-byte variable counts). New entry point `readNeutralBinary` for XT data starting at `P S 0 0`, as in a JT XT B-Rep element. Bytes after the terminator are reported as `trailingBytes` (a JT element carries integer attribute data there). |
| `include/Import/Parasolid/XtJtLayouts.h`, `src/Import/Parasolid/XtJtLayouts.cpp` | Tracked base layouts (60 node types) for XT embedded in JT, with the Siemens copyright notice and provenance. Not for standalone files. |
| `tests/parasolid/xt_reader_tests.cpp` | Added binary tests (big pointer index, unset values, variable-length, edit script, trailing bytes, every-prefix truncation, legacy key refusal) and a table sanity test. |
| `tests/parasolid/xt_decode_cli.cpp` | New mode `--jt-binary <file>`. |
| `tools/parasolid/xt_decode_binary.py`, `tools/parasolid/verify_layouts_annexf.py` | Python reference for binary and the Annex F cross-check. |

Evidence from the Python reference with the tracked table: the local JT XT element (`out/jt-research/bolt-xt-element.bin`) decodes 906 records to the terminator with 140 bytes left over.

Extra G1 step: run `parasolid_xt_decode --jt-binary out/jt-research/bolt-xt-element.bin` and expect `status complete`, `records 906`, `trailing_bytes 140`, `unresolved_pointers` small.

Still unverified by build: all C++ in this update.

## G1 result (owner run, 15 Nov 2026)

- `parasolid_xt_tests`: all checks passed (exit 0) after two fixes found by the build and the
  tests: a string-literal generation error in the tracked JT table, and a reader bug where a
  final token without a trailing space moved the cursor past the end of the stream, so a
  truncated file threw instead of producing a `truncated` diagnostic.
- `parasolid_xt_decode --jt-binary` on the local JT XT element: complete, 906 records,
  140 trailing bytes, 12 unresolved pointers (matches the Python reference).
- Text samples (run before the cursor fix; the fix does not touch complete streams): 8 complete,
  1 stopped at node type 151, 3 refused as legacy keys; status, record counts and unresolved
  pointer counts identical to the Python decoder for all 12.
- Pending: re-run the text-sample decode once more with the final build (expected identical).

## Type 151 (15 Nov 2026)

The stop at node type 151 is resolved locally: 151 and 152 are surface analytic-form nodes (cylinder `C`, cone `N`) referenced from SURFACE_DATA. Inferred layouts added to the local layout file; the previously stopped file then decodes to its terminator (645,928 records, 7 unresolved pointers). One occurrence of each in one file, so this is an observation, not a specification. Expect 9 complete, 3 legacy refusals on the next sample run.

## Stage X2 (15 Nov 2026): model view and topology validation

`XtModel.h/.cpp` give field access by name over a decoded `Xt::File` and `validateTopology`,
which walks shell -> face -> loop -> fin ring and edge -> fin (`other`) ring, resolves every
topology pointer (dangling and wrong-kind counts), and reports counts, fins per edge and
V-E+F per shell. Unit tests use an invented reduced layout. Owner authorised the agent to run
the standalone parasolid tests itself (15 Nov 2026); GUI/viewer runs stay with the owner.

Regenerate: `parasolid_xt_decode --topology out/parasolid-research/layouts-v13.txt <samples> > out/parasolid-research/topology-cpp.json`.

Result on the nine decodable local files (observation): no dangling or wrong-kind pointers,
no ring errors, every edge has exactly two fins, every face, loop and loop-fin is reached from
a shell; two files have fins with no loop (8977 and 24; wire edges), reported as informational.
Gate G2 (counts agree with an independent recomputation) still needs an owner-accepted
second count; the C++ walk and the reader's own per-type counts agree for all nine.
