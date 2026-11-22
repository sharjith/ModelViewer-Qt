# SPDX-License-Identifier: GPL-3.0-only
"""Inventory the OLE streams of a local CAD research sample."""

import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sample", type=Path, help="path to the original sample")
    parser.add_argument("--hash-streams", action="store_true", help="also hash stream payloads")
    args = parser.parse_args()

    try:
        import olefile
    except ModuleNotFoundError as error:
        if error.name != "olefile":
            raise
        parser.exit(1, "Install olefile 0.47 for this research tool.\n")

    try:
        with args.sample.open("rb") as source:
            digest = hashlib.sha256()
            file_size = 0
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
                file_size += len(block)
            source.seek(0)

            with olefile.OleFileIO(source, raise_defects=olefile.DEFECT_INCORRECT) as container:
                streams = [
                    {"path": path, "size_bytes": container.get_size(path)}
                    for path in sorted(container.listdir(streams=True, storages=False))
                ]
                if args.hash_streams:
                    for stream in streams:
                        with container.openstream(stream["path"]) as payload:
                            stream["sha256"] = hashlib.sha256(payload.read()).hexdigest()
    except OSError as error:
        parser.exit(1, f"Could not inventory the compound file: {error}\n")

    print(json.dumps({
        "file_sha256": digest.hexdigest(),
        "file_size_bytes": file_size,
        "olefile_version": olefile.__version__,
        "streams": streams,
    }, indent=2))


if __name__ == "__main__":
    main()
