#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Saves Solid Edge documents as .jt through the local Solid Edge MCP bridge.

Drives the stdio MCP server built from the owner's local SolidEdge-MCP project (tools solid_edge_import_step,
solid_edge_open, solid_edge_save_as, solid_edge_close_active, solid_edge_quit). Each input is either a STEP file (imported
through a part template, saved as <name>.par next to the output) or an existing Solid Edge .par/.asm (opened as is). The
JT is written by Solid Edge with its current default JT options; this script cannot change those. Inputs are never
modified: outputs go to --folder, which must be under the ignored out/ folder. See research/jt/JT_C3_TEST_SAMPLE_PLAN.md.
"""
import argparse
import json
import subprocess
import sys
import threading
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path, help="path of solid-edge-mcp.exe")
    parser.add_argument("--folder", required=True, type=Path, help="output folder under out/")
    parser.add_argument("inputs", nargs="+", type=Path, help=".step, .par or .asm files (absolute paths)")
    args = parser.parse_args()
    folder = args.folder.resolve()
    if not folder.is_relative_to(REPO / "out"):
        raise SystemExit("Folder must be under the ignored out/ folder")
    folder.mkdir(parents=True, exist_ok=True)
    proc = subprocess.Popen([str(args.server)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, bufsize=1)
    threading.Thread(target=lambda: [print("[server stderr]", line.rstrip(), file=sys.stderr, flush=True) for line in proc.stderr],
                     daemon=True).start()
    counter = [0]

    def send(method, params=None, notify=False):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        if not notify:
            counter[0] += 1
            message["id"] = counter[0]
        proc.stdin.write(json.dumps(message) + "\n")
        proc.stdin.flush()
        while not notify:
            line = proc.stdout.readline()
            if not line:
                raise SystemExit("MCP server closed the connection")
            reply = json.loads(line)
            if reply.get("id") == message["id"]:
                return reply

    def call(tool, **arguments):
        reply = send("tools/call", {"name": tool, "arguments": arguments})
        result = reply.get("result", {})
        if result.get("isError") or "error" in reply:
            raise SystemExit(f"{tool} failed: {json.dumps(reply)[:600]}")
        return result.get("structuredContent", result)

    send("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                        "clientInfo": {"name": "solid_edge_export_jt", "version": "1"}})
    send("notifications/initialized", notify=True)
    print("Solid Edge", call("solid_edge_status", visible=False).get("version"))
    for source in args.inputs:
        source = source.resolve()
        jt = folder / f"{source.stem}.jt"
        if source.suffix.lower() == ".step" or source.suffix.lower() == ".stp":
            call("solid_edge_import_step", stepPath=str(source), outputPar=str(folder / f"{source.stem}.par"), overwrite=True,
                 closeAfter=False)
        else:
            call("solid_edge_open", path=str(source), visible=False)
        call("solid_edge_save_as", path=str(jt), overwrite=True)
        call("solid_edge_close_active", save=False)
        print(f"{source.name}: wrote {jt.name} ({jt.stat().st_size} bytes)")
    call("solid_edge_quit", save=False)
    proc.stdin.close()
    proc.wait(timeout=30)


if __name__ == "__main__":
    main()
