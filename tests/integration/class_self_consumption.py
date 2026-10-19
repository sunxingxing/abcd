#!/usr/bin/env python3
"""Exercise certified class-inner binding through the real record pipeline."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


engine, fixtures, output = sys.argv[1:]
fixtures, output = Path(fixtures), Path(output)
output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="run-", dir=output) as scratch:
    for name in ("self", "outer", "throwing", "reentry", "branches", "copies", "accessors", "outer_tdz"):
        destination = Path(scratch) / name
        subprocess.run([engine, str(fixtures / f"{name}.abc"), str(destination),
                        "--value-source", "--language-source"], check=True,
                       capture_output=True, text=True)
        manifest = json.loads((destination / "manifest.json").read_text())
        records = [record for record in manifest["records"]
                   if "class_self_initialization" in record.get("source_failure", {})]
        assert len(records) == 1, (name, records)
        record = records[0]
        failure = record["source_failure"]
        evidence = failure["class_self_initialization"]
        # Enum ordinals are append-only serialized boundary mappings.
        assert failure["causes"] == [{"domain": "abcd::source::ClassSelfError", "value": 5}]
        assert evidence["recovery"]["kind"] == 13  # kRecoveryGate
        assert evidence["recovery"]["projection"] == (0 if name == "outer_tdz" else 1)
        if name == "outer":
            assert evidence["bound_outer"] == 1
        if name == "outer_tdz":
            assert "bound_outer" not in evidence
        assert evidence["tree_references"] > 0
        assert evidence["syntax_owners"] == evidence["scope_view"]["owners"]
        assert evidence["recovery"]["location"]["method"] == evidence["creator" if name == "outer" else "initializer"]
        assert not record["source_complete"] and not record["executable"]
        print(name, "kRecoveryGate", evidence["recovery"]["projection"],
              "self references", evidence["tree_references"], flush=True)
