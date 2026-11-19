# SPDX-License-Identifier: GPL-3.0-only
"""Compare native saved B-rep extraction against a separate compound reader."""

import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import zlib

import olefile


MAX_BYTES = 512 * 1024 * 1024


def configure_zstd(path):
    library = ctypes.CDLL(str(path.resolve()))
    library.ZSTD_getFrameContentSize.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    library.ZSTD_getFrameContentSize.restype = ctypes.c_uint64
    library.ZSTD_decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t]
    library.ZSTD_decompress.restype = ctypes.c_size_t
    return library


def reference_payload(path, zstd):
    label = "PmBRepSegment".encode("utf-16le")
    with olefile.OleFileIO(str(path), raise_defects=olefile.DEFECT_INCORRECT) as container:
        candidates = []
        for stream in container.listdir():
            if len(stream) != 2 or stream[0] != "RSeStorage" or not stream[1].startswith("M"):
                continue
            with container.openstream(stream) as metadata:
                if label in metadata.read(128):
                    candidates.append([stream[0], "B" + stream[1][1:]])
        if len(candidates) != 1:
            raise ValueError("Expected exactly one saved B-rep segment")
        with container.openstream(candidates[0]) as stream:
            data = stream.read()

    envelope, compressed = data[16:18], data[18:]
    if envelope == b"\x04\x01":
        decoder = zlib.decompressobj()
        payload = decoder.decompress(compressed, MAX_BYTES)
        if not decoder.eof:
            raise ValueError("Incomplete or oversized zlib payload")
        codec = "zlib"
    elif envelope == b"\x04\x02":
        size = zstd.ZSTD_getFrameContentSize(compressed, len(compressed))
        if size > MAX_BYTES:
            raise ValueError("Invalid or oversized Zstandard payload")
        buffer = ctypes.create_string_buffer(size)
        if zstd.ZSTD_decompress(buffer, size, compressed, len(compressed)) != size:
            raise ValueError("Could not decompress the reference Zstandard payload")
        payload = buffer.raw
        codec = "zstd"
    else:
        raise ValueError("Unknown compression envelope")
    return codec, payload


def reference_segment(path, zstd):
    codec, payload = reference_payload(path, zstd)
    return codec, {
        "size_bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "asm_header_offset": payload.find(b"ASM BinaryFile4"),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--zstd-library", type=Path, required=True)
    parser.add_argument("--sample", nargs=2, action="append", required=True, metavar=("ID", "PATH"))
    args = parser.parse_args()

    library = configure_zstd(args.zstd_library)
    results = {}
    for sample_id, source in args.sample:
        codec, reference = reference_segment(Path(source), library)
        native = json.loads(subprocess.check_output([args.probe, "--part-brep", source]))
        if native != reference or native["asm_header_offset"] < 0:
            parser.exit(1, f"Saved B-rep verification differs for {sample_id}\n")
        results[sample_id] = {"compression": codec, **native}
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
