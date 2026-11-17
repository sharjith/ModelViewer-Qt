#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Inventory standalone XT prefixes and hashes without decoding geometry."""

import argparse
import hashlib
import json
import re
from pathlib import Path


PREFIX_LIMIT = 64 * 1024
HEADER_END = re.compile(rb"(?m)^\*\*END_OF_HEADER\**[ \t]*\r?\n")
SCHEMA_KEY = re.compile(rb"SCH_[0-9]+_[0-9]+(?:_[0-9]+)?")


def read_string(data, offset):
    """Read one length-prefixed string from the normalized text prefix."""
    length = re.match(rb"[0-9]{1,5} ", data[offset:offset + 6])
    if length is None:
        raise ValueError("missing string length")
    start = offset + length.end()
    end = start + int(length[0])
    if end > len(data):
        raise ValueError("incomplete string")
    return data[start:end], end


def inspect_prefix(data):
    fields = {}
    if not data:
        return {"status": "empty"}

    trailer = HEADER_END.search(data)
    if trailer:
        header = data[:trailer.start()].replace(b"\r", b"").replace(b"\n", b"")
        # Mask escapes before splitting so an escaped semicolon stays in its field.
        for field in re.sub(rb"\^.", b"?", header).split(b";")[:-1]:
            if field.startswith(b"SCH=") and SCHEMA_KEY.fullmatch(field[4:]):
                fields["common_schema"] = field[4:].decode("ascii")
                break
        data = data[trailer.end():]
    elif data.startswith(b"**"):
        return {"status": "common header trailer absent within prefix"}

    if not data.startswith(b"T"):
        return {**fields, "status": "text transport marker absent"}

    # Physical line endings can split a count, a string, or the next field.
    data = b"".join(line.rstrip(b" ") for line in data.replace(b"\r", b"").split(b"\n"))
    try:
        modeller, offset = read_string(data, 1)
        schema, offset = read_string(data, offset)
        version = re.fullmatch(
            rb": TRANSMIT FILE(?: \(partition\))? created by modeller version ([0-9]+)",
            modeller,
        )
        if version is None or not SCHEMA_KEY.fullmatch(schema):
            raise ValueError("unrecognized modeller or schema string")
        count = re.match(rb"[0-9]{1,5} ", data[offset:offset + 6])
        if count is None:
            raise ValueError("missing count after internal schema")
    except ValueError as error:
        return {**fields, "status": str(error)}
    # A base-schema suffix introduces an extra node-type limit before the data.
    count_field = "node_type_limit" if schema.count(b"_") == 3 else "user_field_size"
    return {
        **fields,
        "status": "text prefix inspected",
        "modeller_version": int(version[1]),
        "internal_schema": schema.decode("ascii"),
        count_field: int(count[0]),
    }


def inspect(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        prefix = source.read(PREFIX_LIMIT)
        digest.update(prefix)
        size = len(prefix)
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
            size += len(chunk)
    sha256 = digest.hexdigest()
    return {
        "id": f"XT-{sha256[:12]}",
        "sha256": sha256,
        "bytes": size,
        **inspect_prefix(prefix),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="directory containing local XT samples")
    args = parser.parse_args()
    if not args.root.is_dir():
        parser.error("root must be an existing sample directory")
    rows = [
        inspect(path)
        for path in args.root.rglob("*")
        if path.suffix.lower() in {".x_t", ".xmt_txt"} and path.is_file()
    ]
    print(json.dumps(sorted(rows, key=lambda row: row["id"]), indent=2))


if __name__ == "__main__":
    main()
