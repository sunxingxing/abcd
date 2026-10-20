#!/usr/bin/env python3
"""Real exporter, native class-inner semantics and conservative family gates."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

engine, fixtures, output = sys.argv[1:]
fixtures, output = Path(fixtures).resolve(), Path(output).resolve()
output.mkdir(parents=True, exist_ok=True)
published = ("self", "outer", "branches", "constructor_throw", "reentry", "accessors", "member_nested", "member_throwing")
refused = {"copies": 10, "throwing": 10, "outer_tdz": 0}
runner = r'''
import {pathToFileURL} from 'node:url';
globalThis.select = process.argv[2] === 'true';
globalThis.log = [];
if (process.argv[3] === 'setter') Object.defineProperty(Function.prototype, 'x', {
  configurable:true, set(value) { globalThis.log.push('prototype-set:' + this.name); },
  get() { return 'inherited'; }
});
let value;
try {
  const {C} = await import(pathToFileURL(process.argv[1]));
  const descriptor = Object.getOwnPropertyDescriptor(C, 'x');
  value = {self:C.x === C, name:C.name, length:C.length, value:typeof C.x === 'number' ? C.x : C.x instanceof C,
           own:!!descriptor, writable:descriptor?.writable, configurable:descriptor?.configurable,
           enumerable:descriptor?.enumerable};
  if ('value' in C.prototype) {
    const instance = new C(); instance.value = 4;
    value.accessor = [instance.value === C, instance.stored, C.current === C];
  }
} catch(e) { value = {error:e.constructor.name}; }
console.log(JSON.stringify({value,log:globalThis.log}));
'''
with tempfile.TemporaryDirectory(prefix="run-", dir=output) as scratch:
    for name in (*published, *refused):
        destination = Path(scratch) / name
        subprocess.run([engine, str(fixtures / f"{name}.abc"), str(destination),
                        "--value-source", "--language-source"], check=True,
                       capture_output=True, text=True)
        manifest = json.loads((destination / "manifest.json").read_text())
        records = [r for r in manifest["records"] if r.get("source_status") in (0, 3)]
        assert len(records) == 1, (name, records)
        record = records[0]
        if name in published:
            assert record["source_complete"] and record["source_status"] == 0, (name, record)
            printed = destination / record["source"]
            subprocess.run(["node", "--check", str(printed)], check=True)
            original = Path(scratch) / f"{name}.mjs"
            original.write_text((fixtures / f"{name}.js").read_text())
            for select, prototype in (("false","plain"),("true","plain"),("false","setter"),("true","setter")):
                outcomes = [subprocess.run(["node", "--input-type=module", "-e", runner,
                                           str(source), select, prototype], check=True,
                                          capture_output=True, text=True).stdout
                            for source in (original, printed)]
                assert outcomes[0] == outcomes[1], (name, outcomes)
                result = json.loads(outcomes[0])
                if name in ("constructor_throw", "member_throwing"):
                    assert result == {"value":{"error":"RangeError"},"log":["constructor" if name == "constructor_throw" else "member-throw"]}
                elif prototype == "plain":
                    assert result["value"]["name"] == "C"
                    assert result["value"]["value"] == (7 if name == "outer" else name not in ("reentry", "member_nested"))
                    if name in ("reentry", "member_nested"):
                        assert result["value"]["self"] is True
                elif name == "outer":
                    assert result["log"] == ["prototype-set:C"] and not result["value"]["own"]
                else:
                    assert not result["log"] and result["value"]["own"]
            print(name, "published, node --check, 4 engine-output differentials", flush=True)
            continue
        failure = record["source_failure"]
        evidence = failure["class_self_initialization"]
        assert failure["causes"] == [{"domain": "abcd::source::ClassSelfError", "value": 5}]
        assert evidence["recovery"]["kind"] == 13
        assert evidence["recovery"]["projection"] == refused[name]
        assert not record["source_complete"] and not record["executable"]
        if name in ("copies", "throwing"):
            assert evidence["recovery"]["location"]["method"] == failure["method"]
            assert "expression" in evidence["recovery"]["location"]
            # Refused engine output has no executable class. Record the source
            # oracle without labelling it an engine-output differential.
            original = Path(scratch) / f"{name}.mjs"
            original.write_text((fixtures / f"{name}.js").read_text())
            observed = subprocess.run(["node", "--input-type=module", "-e", runner,
                                       str(original), "false", "plain"], check=True,
                                      capture_output=True,text=True)
            value = json.loads(observed.stdout)
            assert value == ({"value":{"error":"RangeError"},"log":[]} if name=="throwing" else
                             {"value":{"self":True,"name":"C","length":0,"value":False,
                                       "own":True,"writable":True,"configurable":True,"enumerable":True},"log":[]})
        print(name, "kRecoveryGate", refused[name], flush=True)
