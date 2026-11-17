#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Schema-driven text XT record decoder (research tool, read-only).

Reads the common header, the internal prefix, the embedded-schema edit scripts
and every record, and reports whether the stream is consumed exactly up to the
terminator. It builds no geometry.

The base field layouts are NOT part of this repository. They are read from a
local JSON file (see README.md) that the owner produces from a lawfully held
copy of the publisher's reference. Without it the tool refuses to run.

Usage:
    python -I tools/parasolid/xt_decode.py LAYOUTS.json SAMPLE_OR_DIR > out/parasolid-research/decode.json
"""

import argparse
import collections
import hashlib
import json
import re
import sys
from pathlib import Path

HEADER_END = re.compile(r"\*\*END_OF_HEADER\**[ \t]*\r?\n")
SCHEMA_KEY = re.compile(r"SCH_[0-9]+_[0-9]+(?:_[0-9]+)?")


class DecodeError(Exception):
    pass


class Reader:
    """Cursor over the normalised text stream (newlines and trailing spaces removed)."""

    def __init__(self, buf, pos=0):
        self.buf, self.pos = buf, pos

    def token(self):
        if self.buf[self.pos:self.pos + 1] == "?":  # unset value is not space-terminated
            self.pos += 1
            return "?"
        end = self.buf.find(" ", self.pos)
        if end < 0:
            end = len(self.buf)
        text = self.buf[self.pos:end]
        self.pos = end + 1
        return text

    def char(self):
        if self.pos >= len(self.buf):
            raise DecodeError("unexpected end of stream")
        c = self.buf[self.pos]
        self.pos += 1
        return c

    def string(self):
        n = int(self.token())
        if n < 0 or self.pos + n > len(self.buf):
            raise DecodeError("string length out of range")
        s = self.buf[self.pos:self.pos + n]
        self.pos += n
        return s


def number(text, cast):
    return text if text == "?" else cast(text)


def read_value(r, t):
    if t in "dpnuw":
        return number(r.token(), int)
    if t == "f":
        return number(r.token(), float)
    if t in "cl":
        return r.char()
    if t == "v":
        a = r.token()
        return "?" if a == "?" else (float(a), float(r.token()), float(r.token()))
    if t == "h":
        return tuple(float(r.token()) for _ in range(3))
    if t == "i":
        return tuple(float(r.token()) for _ in range(2))
    if t == "b":
        return tuple(float(r.token()) for _ in range(6))
    raise DecodeError("unknown field type %r" % t)


def read_field_definition(r):
    name = r.string()
    ptr_class = int(r.token())
    n_elts = int(r.token())
    ftype = "p" if ptr_class else r.string()
    if not ptr_class and n_elts == 1:
        r.char()  # transmit flag, present only for variable-length fields
    return (name, ftype, n_elts)


def normalise(raw):
    return "".join(line.rstrip(" ") for line in raw.replace("\r", "").split("\n"))


def load_layouts(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    return {int(k): (v["name"], [tuple(f) for f in v["fields"]]) for k, v in data["types"].items()}


def decode(raw, base):
    match = HEADER_END.search(raw)
    if not match:
        raise DecodeError("common header trailer not found")
    body = normalise(raw[match.end():])
    if not body.startswith("T"):
        raise DecodeError("text transport marker absent (binary or unknown)")
    r = Reader(body, 1)
    r.string()  # modeller version string
    schema = r.string()
    if not SCHEMA_KEY.fullmatch(schema):
        raise DecodeError("unrecognised schema key")
    embedded = schema.count("_") == 3
    type_limit = int(r.token()) if embedded else None
    r.token()  # user field size
    result = {"internal_schema": schema, "embedded": embedded, "node_type_limit": type_limit}
    layouts, counts, new_types = {}, collections.Counter(), {}
    seen, pointers = set(), []
    nodes = 0
    t = None
    try:
        while True:
            t = int(r.token())
            if t == 1:
                int(r.token())
                result["status"] = "complete" if r.pos >= len(body) else "terminator before end of stream"
                break
            if t not in layouts:
                if not embedded:
                    raise DecodeError("legacy unsuffixed schema: no layout available")
                if r.buf[r.pos:r.pos + 3] == "255":  # file's layout equals the base layout
                    r.token()
                    if t not in base:
                        raise DecodeError("base layout unknown for node type %d" % t)
                    layouts[t] = base[t]
                else:
                    field_count = int(r.token())  # fields in the current schema
                    if t in base:
                        base_fields, current, i = base[t][1], [], 0
                        while True:
                            op = r.char()
                            if op == "C":
                                current.append(base_fields[i]); i += 1
                            elif op == "D":
                                i += 1
                            elif op in "IA":
                                current.append(read_field_definition(r))
                            elif op == "Z":
                                if i != len(base_fields):
                                    raise DecodeError("edit script does not consume base layout of type %d" % t)
                                break
                            else:
                                raise DecodeError("bad edit operation %r" % op)
                        if len(current) != field_count:
                            raise DecodeError("edit script of type %d yields %d fields, declared %d" % (t, len(current), field_count))
                        layouts[t] = (base[t][0], current)
                    else:
                        name = r.string()
                        r.string()  # description
                        layouts[t] = (name, [read_field_definition(r) for _ in range(field_count)])
                        new_types[t] = name
            name, fields = layouts[t]
            variable = bool(fields) and fields[-1][2] == 1
            n_var = int(r.token()) if variable else 0
            index = int(r.token())
            seen.add(index)
            for fname, ftype, n_elts in fields:
                if n_elts == 0:
                    v = read_value(r, ftype)
                    if ftype == "p" and isinstance(v, int) and v > 0:
                        pointers.append((name, fname, v))
                else:
                    for _ in range(n_var if n_elts == 1 else n_elts):
                        read_value(r, ftype)
            counts[name] += 1
            nodes += 1
    except (DecodeError, ValueError, IndexError) as error:
        result["status"] = "stopped"
        result["error"] = str(error)
        result["stopped_at_node_type"] = t
    unresolved = collections.Counter((n, f) for n, f, v in pointers if v not in seen)
    result.update(records=nodes, counts=dict(counts), new_types=new_types,
                  unresolved_pointers=sum(unresolved.values()),
                  unresolved_by_field={"%s.%s" % k: v for k, v in unresolved.most_common(8)})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("layouts", type=Path, help="local base-layout JSON (not tracked)")
    parser.add_argument("sample", type=Path, help="X_T file or directory")
    args = parser.parse_args()
    if not args.layouts.is_file():
        parser.error("layout file not found; see tools/parasolid/README.md")
    base = load_layouts(args.layouts)
    files = [args.sample] if args.sample.is_file() else sorted(
        p for p in args.sample.rglob("*") if p.suffix.lower() in {".x_t", ".xmt_txt"} and p.is_file())
    rows = []
    for path in files:
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        row = {"id": "XT-" + digest[:12], "sha256": digest, "bytes": path.stat().st_size}
        try:
            row.update(decode(path.read_bytes().decode("latin-1"), base))
        except DecodeError as error:
            row.update(status="refused", error=str(error))
        rows.append(row)
    print(json.dumps(sorted(rows, key=lambda r: r["id"]), indent=2))


if __name__ == "__main__":
    sys.exit(main())
