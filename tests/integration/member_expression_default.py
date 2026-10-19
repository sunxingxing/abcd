#!/usr/bin/env python3
"""Pin an honest ABI refusal until effectful parameter expressions are proven."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

engine, probe, fixture, output = sys.argv[1:]
output = Path(output)
output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=output) as scratch:
    destination = Path(scratch) / "export"
    subprocess.run([engine, fixture, str(destination), "--value-source",
                    "--language-source"], check=True, capture_output=True)
    manifest = json.loads((destination / "manifest.json").read_text())
    records = [r for r in manifest["records"] if r.get("source_status") in (0, 3)]
    assert len(records) == 1, records
    record = records[0]
    assert not record["source_complete"] and record["source_status"] != 0
    failure = record["source_failure"]
    assert failure["causes"] == [{"domain": "abcd::source::ClassSelfError", "value": 5}]
    recovery = failure["class_self_initialization"]["recovery"]
    assert recovery["projection"] == 9 and recovery["kind"] == 13, recovery
    lengths = recovery["abi_lengths"]
    assert lengths["declared"] == 1 and lengths["signature"] == 7, lengths
    assert failure["method"] == recovery["location"]["method"] == lengths["method"]
    subprocess.run([probe, fixture, str(lengths["method"])], check=True)
    print("guarded module-property and global-property defaults keep the member ABI gate")
