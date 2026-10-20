#!/usr/bin/env python3
"""Completed-class uses are different from exporting an invocation's result."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

engine, fixtures, output = sys.argv[1:]
fixtures, output = Path(fixtures).resolve(), Path(output).resolve()
output.mkdir(parents=True, exist_ok=True)
runner = r'''
import {pathToFileURL} from 'node:url';
globalThis.log = [];
try {
  const {s} = await import(pathToFileURL(process.argv[1]));
  console.log(JSON.stringify({name:s.constructor.name,
    same:s === s.constructor.getInstance(), log:globalThis.log}));
} catch(e) { console.log(JSON.stringify({error:e.constructor.name, log:globalThis.log})); }
'''
with tempfile.TemporaryDirectory(prefix="run-", dir=output) as scratch:
    scratch = Path(scratch)
    for name in ("completed", "completed_throw", "completed_self", "completed_early"):
        destination = scratch / name
        subprocess.run([engine, str(fixtures / f"{name}.abc"), str(destination),
                        "--value-source", "--language-source"], check=True,
                       capture_output=True, text=True)
        rows = json.loads((destination / "manifest.json").read_text())["records"]
        record, = [r for r in rows if r.get("source_status") in (0, 3)]
        if name in ("completed_self", "completed_early"):
            failure = record["source_failure"]
            assert not record["source_complete"] and not record["executable"]
            assert failure["causes"] == [{"domain":"abcd::source::ClassSelfError", "value":5}]
            recovery = failure["class_self_initialization"]["recovery"]
            assert recovery["kind"] == 13
            # Member-inner resolution and early exposure have separate gates.
            assert recovery["projection"] == (3 if name == "completed_self" else 4)
            print(name, "typed recovery gate", recovery["projection"], flush=True)
            continue
        assert record["source_complete"] and record["source_status"] == 0, record
        printed = destination / record["source"]
        subprocess.run(["node", "--check", str(printed)], check=True)
        original = scratch / f"{name}.mjs"
        original.write_text((fixtures / f"{name}.js").read_text())
        observed = [subprocess.run(["node", "--input-type=module", "-e", runner,
                                    str(source)], check=True, capture_output=True,
                                   text=True).stdout for source in (original, printed)]
        assert observed[0] == observed[1], (name, observed)
        assert json.loads(observed[0]) == (
            {"name":"S", "same":True, "log":[]} if name == "completed" else
            {"error":"RangeError", "log":["get"]})
        print(name, "published; node --check and engine-output differential", flush=True)
