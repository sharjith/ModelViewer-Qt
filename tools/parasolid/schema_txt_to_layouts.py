#!/usr/bin/env python3
"""Convert a Parasolid schema text file (sch_<n>.sch_txt) into the one-node-type-per-line layout table the importer reads.

Usage: schema_txt_to_layouts.py <sch_n.sch_txt> > layouts.txt

Node header:  <type> <NAME>; <description>; <a> <b> <variable>
Field line:   <name>; <type>; <transmitted> <class> <count>
A field is kept when <transmitted> is 1. <count> 0 is a scalar, 1 on the last field of a variable-length node is an
open array '[]', larger values a fixed array '[n]'. The schema files are Siemens data and are not part of this repository.
"""
import re
import sys

NODE = re.compile(r"^(\d+) ([A-Za-z0-9_]+); (.*?); (\d+) (\d+) (\d+)\s*$")
FIELD = re.compile(r"^([A-Za-z0-9_]+); (\w); (\d+) (\d+) (\d+)\s*$")


def convert(text):
    lines = text.replace("\r", "").split("\n")
    start = next(i for i, l in enumerate(lines) if l.startswith("**END_OF_HEADER"))
    out, cur = [], None
    for line in lines[start + 1:]:
        node = NODE.match(line)
        if node:
            if cur:
                out.append(cur)
            cur = [int(node.group(1)), node.group(2), int(node.group(6)), []]
            continue
        field = FIELD.match(line)
        if field and cur is not None and field.group(3) == "1":
            count = int(field.group(5))
            cur[3].append((field.group(1), field.group(2), count))
    if cur:
        out.append(cur)
    rows = []
    for ntype, name, variable, fields in out:
        parts = []
        for i, (fname, ftype, count) in enumerate(fields):
            suffix = ""
            if count > 1:
                suffix = "[%d]" % count
            elif count == 1 and variable and i == len(fields) - 1:
                suffix = "[]"
            parts.append("%s:%s%s" % (fname, ftype, suffix))
        rows.append("%d %s %s" % (ntype, name, " ".join(parts)))
    return rows


if __name__ == "__main__":
    with open(sys.argv[1], "r", encoding="latin-1") as handle:
        print("\n".join(convert(handle.read())))
