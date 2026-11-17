# tools/parasolid

Research tooling for Parasolid X_T. Original code; no vendor tables are stored here.

| Tool | Purpose |
|---|---|
| `inspect_xt.py` | Prefix and hash inventory of local samples (path-free output). |
| `xt_decode.py` | Schema-driven decode of every record in a text X_T file. Reports whether the stream is consumed exactly to the terminator, per-type record counts and unresolved pointers. Builds no geometry. |

## Base layouts are not tracked

`xt_decode.py` needs a JSON file of base field layouts, kept at `out/parasolid-research/layouts-v13.json` (ignored by git). The publisher's reference reserves its copyright, so a transcription of its tables is **not** committed. The owner produces the file from a lawfully held copy of the reference.

Format: `{"types": {"<node type number>": {"name": "...", "fields": [["field", "<type>", <n_elts>], ...]}}}`.
Field types: `d` int, `p` pointer, `f` double, `v` vector, `c` char, `l` logical, `n` short, `u` byte, `i` interval, `b` box, `h` three doubles. `n_elts` is 0 for a scalar, 1 for a variable-length last field, or an array size.
The layouts must describe the base schema that embedded edit scripts are measured against (V13), not the newest published layout; the decoder asserts that every edit script consumes the base exactly, so a wrong table fails loudly.

## Regenerate the evidence

```powershell
python -I tools/parasolid/xt_decode.py out/parasolid-research/layouts-v13.json "<local-samples>" > out/parasolid-research/decode.json
```
