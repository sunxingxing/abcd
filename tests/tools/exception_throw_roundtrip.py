#!/usr/bin/env python3
"""Compare the catch-payload fixture and engine output without normalization.

Uses grouping_bytecode.py's pinned SDK/assembly helpers. Every method,
exception range, operand, literal and module entry remains in the strict diff.
Node traces and typed close-control refusals are separate acceptance layers.
"""
import argparse
import difflib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("grouping_bytecode", HERE / "grouping_bytecode.py")
grouping = importlib.util.module_from_spec(spec)
spec.loader.exec_module(grouping)


def methods(text):
    return re.findall(r"(?ms)^\.function .*?^}\n", text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("engine")
    parser.add_argument("fixtures", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--temporary", action="store_true")
    args = parser.parse_args()
    if args.temporary:
        args.output.mkdir(parents=True, exist_ok=True)
        scratch = tempfile.TemporaryDirectory(dir=args.output)
        args.output = Path(scratch.name)
    else:
        args.output.mkdir(parents=True, exist_ok=False)
    report = {"compiler": {"path": grouping.ES2ABC, "sha256": grouping.sha(grouping.ES2ABC)},
              "disassembler": {"path": grouping.DISASM, "sha256": grouping.sha(grouping.DISASM)},
              "records": []}
    for name in ("handler",):
        folder = args.output / name
        folder.mkdir()
        exported = folder / "export"
        subprocess.run([args.engine, str(args.fixtures / (name + ".abc")), str(exported),
                        "--value-source", "--language-source"], check=True, capture_output=True)
        record, = [r for r in json.loads((exported / "manifest.json").read_text())["records"]
                   if r["source_status"] != 2]
        assert record["source_status"] == 0, record
        emitted = exported / record["source"]
        subprocess.run(["node", "--check", str(emitted)], check=True)
        row = {"fixture": name, "bytecode": {}}
        for mode in ("default", "O0"):
            texts = {}
            commands = {}
            for side, source in (("original", args.fixtures / (name + ".js")), ("emitted", emitted)):
                dest = folder / (side + "-" + mode)
                dest.mkdir()
                path = dest / "input.js"
                path.write_text(source.read_text())
                # Same AST adaptation on both sides: the SDK predates JS
                # string-named exports. Only identifier-valued aliases change.
                adaptation = subprocess.run(["node", "--expose-internals", "--input-type=module", "-e", r"""
import {readFileSync,writeFileSync} from 'node:fs';
import {createRequire} from 'node:module';
const acorn=createRequire(import.meta.url)('internal/deps/acorn/acorn/dist/acorn');
const file=process.argv[1];let source=readFileSync(file,'utf8');
const ast=acorn.parse(source,{ecmaVersion:'latest',sourceType:'module'}),edits=[];
for(const n of ast.body)if(n.type==='ExportNamedDeclaration')for(const spec of n.specifiers){
 if(spec.exported.type!=='Literal'||typeof spec.exported.value!=='string')continue;
 const name=spec.exported.value;
 try{const a=acorn.parse(`const x=0;export {x as ${name}};`,{ecmaVersion:'latest',sourceType:'module'});
 if(a.body[1].specifiers[0].exported.type==='Identifier')edits.push([spec.exported.start,spec.exported.end,name]);}catch{}
}
for(const [a,b,text] of edits.sort((a,b)=>b[0]-a[0]))source=source.slice(0,a)+text+source.slice(b);
writeFileSync(file,source);console.log(edits.length);
""", str(path)], check=True, capture_output=True, text=True)
                row.setdefault("export_alias_adaptations", {})[side + "-" + mode] = int(adaptation.stdout)
                abc, pa = dest / "input.abc", dest / "input.pa"
                command = [grouping.ES2ABC, "--module", "--merge-abc",
                           "--record-name=exception_throw_" + name, "--source-file=input.js",
                           "--extension=js", "--output=" + str(abc)]
                if mode == "O0":
                    command.append("--opt-level=0")
                command.append(str(path))
                commands[side] = command
                assert grouping.run(command, dest / "compile.log") == 0
                assert grouping.run([grouping.DISASM, str(abc), str(pa)], dest / "disasm.log") == 0
                texts[side] = grouping.assembly(pa)
            left, right = methods(texts["original"]), methods(texts["emitted"])
            assert len(left) == len(right) == 2, (name, mode, len(left), len(right))
            # Function names may differ. Preserve file order and retain all
            # bodies/annotations/metadata, without register or edge normalization.
            rows = []
            for i, (a, b) in enumerate(zip(left, right)):
                assert a.splitlines()[0] == b.splitlines()[0], (name, mode, "method identity")
                diff = "".join(difflib.unified_diff(a.splitlines(True), b.splitlines(True),
                                                fromfile="original", tofile="emitted"))
                (folder / f"{mode}-method{i}.diff").write_text(diff)
                rows.append({"original": a.splitlines()[0], "emitted": b.splitlines()[0],
                             "equal": a == b})
            (folder / (mode + ".diff")).write_text("".join(difflib.unified_diff(
                texts["original"].splitlines(True), texts["emitted"].splitlines(True))))
            row["bytecode"][mode] = {"equal": texts["original"] == texts["emitted"],
                                     "methods": rows, "commands": commands}
        report["records"].append(row)
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"recompiled": len(report["records"]) * 2,
                      "methods_compared": 4,
                      "identical_records": sum(v["equal"] for r in report["records"] for v in r["bytecode"].values()),
                      "identical_methods": sum(m["equal"] for r in report["records"] for v in r["bytecode"].values() for m in v["methods"])}))


if __name__ == "__main__":
    main()
