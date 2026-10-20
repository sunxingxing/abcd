#!/usr/bin/env python3
"""Recover guarded member defaults through the generic parameter proof."""
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
    assert record["source_complete"] and record["source_status"] == 0, record
    source=destination/record["source"]
    subprocess.run(["node","--check",str(source)],check=True)
    text=source.read_text()
    assert "Mode.Normal" in text and "globalThis.Alignment.Center" in text
    assert "static" in text and "new C(1)" in text
    traces=[]
    for side,program in (("original",Path(fixture).with_suffix(".js").read_text()),("emitted",text)):
        folder=Path(scratch)/side;folder.mkdir()
        (folder/"input.mjs").write_text(program.replace("'./mode.js'","'./mode.mjs'").replace('"./mode.js"','"./mode.mjs"'))
        (folder/"mode.mjs").write_text('export const Mode=globalThis.Mode;')
        (folder/"run.mjs").write_text('''const log=[];globalThis.Mode={get Normal(){log.push('Normal');return 2;}};globalThis.Alignment={get Center(){log.push('Center');return 3;}};
const {C}=await import('./input.mjs');const result=[C.length,C.instance instanceof C,Object.values(C.instance),Object.values(new C(9)),Object.values(new C(8,null,0,0,-1,1,0))];console.log(JSON.stringify({result,log}));''')
        trace=json.loads(subprocess.run(["node",str(folder/"run.mjs")],check=True,capture_output=True,text=True).stdout)
        assert trace["result"][0]==1 and trace["result"][1] is True
        traces.append(trace)
    assert traces[0]==traces[1],traces
    subprocess.run([probe,fixture,"461"],check=True)
    print("guarded import/property defaults publish with declared member length 1")
