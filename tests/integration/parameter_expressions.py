#!/usr/bin/env python3
"""Engine-only recovery, independent execution oracle, and exact SDK diffs.

The paired JS is used only by Node/the compiler oracle, never by the engine.
All differing disassembly is retained without register/branch normalization.
"""
import difflib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
engine, fixtures, output = sys.argv[1:]
fixtures = Path(fixtures).resolve()
root = Path(output).resolve()
root.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix="run-", dir=root))
spec = importlib.util.spec_from_file_location("grouping", Path(__file__).parents[1]/"tools/grouping_bytecode.py")
grouping = importlib.util.module_from_spec(spec)
spec.loader.exec_module(grouping)
setup = '''const log=[]; globalThis.log=log;
const input={get x(){log.push('x');return 17;}};globalThis.input=input;
Object.defineProperty(globalThis,'y',{configurable:true,get(){log.push('y');return 23;}});
'''
cases = {
    "functions": "const f=m.f; const v=[f.length,f('a'),f('b',undefined),f('c',null,false,0,0),f('d',7,8,9,10)];const binding=await import('./m.mjs');binding.update({get x(){log.push('Mnew.x');return 53;}});v.push(f('live'));",
    "earlier": "const f=m.f;const a={get x(){log.push('earlier.x');return 31;}};const v=[f.length,f(a),f(0,{x:4}),f(0,null,5)];",
    "throwing": "const f=m.f;let error;Object.defineProperty(input,'x',{get(){log.push('throw');throw new TypeError('x');}});try{f(1);}catch(e){error=e.constructor.name;}const v=[f.length,error,f(1,0),f(1,null),f(1,false)];",
    "method": "const c=new m.C();const f=c.f;const a=c.f(1);const b=c.f(2,0,1,2);const v=[f.length,a[0]===c,a.slice(1),b[0]===c,b.slice(1)];",
    "captures": "const a=m.make(input),b=m.make({x:41});const v=[a.length,a(1),a(2,undefined),a(3,null),b(4),a(5,0)];",
    "updates": "const a=m.make(input),b=m.make({x:91});const v=[a.length,a(1),b(1)];globalThis.replacement={get x(){log.push('updated.x');return 43;}};v.push(a(99),a(2),a(3,0),b(2));",
}
report = {"compiler":grouping.ES2ABC,"compiler_sha256":grouping.sha(grouping.ES2ABC),"disassembler_sha256":grouping.sha(grouping.DISASM),"fixtures":[]}
for name in (*cases, "later"):
    dest = root/name
    dest.mkdir()
    exported = dest/"export"
    subprocess.run([engine,str(fixtures/f"{name}.abc"),str(exported),"--value-source","--language-source"],check=True,capture_output=True)
    rows = [r for r in json.loads((exported/"manifest.json").read_text())["records"] if r.get("source_status") in (0,3)]
    assert len(rows)==1, rows
    row=rows[0]
    if name=="later":
        assert not row["source_complete"] and row.get("source_failure"),row
        assert row["source_failure"]["causes"]==[{"domain":"abcd::source::DefaultBodyError","value":2}],row
        report["fixtures"].append({"fixture":name,"refusal":row["source_failure"]})
        continue
    assert row["source_complete"],row
    emitted=(exported/row["source"]).read_text()
    node=[];comparisons=[]
    for side,text in (("original",(fixtures/f"{name}.js").read_text()),("emitted",emitted)):
        directory=dest/side
        directory.mkdir()
        js=directory/"input.mjs"
        js.write_text(text.replace("'./m.js'","'./m.mjs'").replace('"./m.js"','"./m.mjs"'))
        (directory/"m.mjs").write_text('export let M=globalThis.input;export function update(v){M=v;}\n')
        subprocess.run(["node","--check",str(js)],check=True,capture_output=True)
        harness=directory/"run.mjs"
        harness.write_text(setup+"const m=await import('./input.mjs');\n"+cases[name]+"\nconsole.log(JSON.stringify({v,log}));\n")
        p=subprocess.run(["node",str(harness)],check=True,text=True,capture_output=True)
        node.append(json.loads(p.stdout))
    assert node[0]==node[1],(name,node)
    for mode in ("default","O0"):
        texts=[];commands=[]
        for side,text in (("original",(fixtures/f"{name}.js").read_text()),("emitted",emitted)):
            directory=dest/side
            js=directory/"compile.js"
            js.write_text(re.sub(r' as "([A-Za-z_$][\w$]*)"',r' as \1',text))
            abc=directory/f"{mode}.abc";pa=abc.with_suffix(".pa")
            cmd=[grouping.ES2ABC,"--module","--merge-abc",f"--record-name={name}","--source-file=input.js","--extension=js",f"--output={abc}"]
            if mode=="O0":cmd.append("--opt-level=0")
            cmd.append(str(js));commands.append(cmd)
            assert grouping.run(cmd,abc.with_suffix(".compile.log"))==0,(name,side,mode)
            assert grouping.run([grouping.DISASM,str(abc),str(pa)],abc.with_suffix(".disasm.log"))==0
            texts.append(grouping.assembly(pa))
        (dest/f"{mode}.diff").write_text(''.join(difflib.unified_diff(texts[0].splitlines(True),texts[1].splitlines(True),fromfile="original",tofile="emitted")))
        # Exact comparison is evidence, not an equality claim after rewriting
        # exports/names/register allocation. Keep all changes for review.
        methods=[dict(re.findall(r"^(\.function [^\n]+)\n(.*?^})",t,re.M|re.S)) for t in texts]
        comparisons.append({"mode":mode,"identical":texts[0]==texts[1],"commands":commands,"methods":[{"method":key,"identical":key in methods[1] and methods[0][key]==methods[1][key]} for key in methods[0]],"added_methods":sorted(methods[1].keys()-methods[0].keys())})
    report["fixtures"].append({"fixture":name,"published":True,"node":"pass","trace":node[1],"comparisons":comparisons})
(root/"summary.json").write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps({"published":len(cases),"refused":1,"node_differentials":len(cases),"roundtrips":2*len(cases),"report":str(root/"summary.json")}))
