#!/usr/bin/env python3
"""Record exact recompilation differences; independently recheck raw class ports.

This is semantic class recovery, not a grouping-only equality test. Do not
normalize register/branch/handler differences or label a changed method equal.
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
engine, probe, fixtures, output = sys.argv[1:]
fixtures, output = Path(fixtures).resolve(), Path(output).resolve()
output.mkdir(parents=True, exist_ok=True)
output = Path(tempfile.mkdtemp(prefix="run-", dir=output))
spec = importlib.util.spec_from_file_location("grouping_bytecode", Path(__file__).parents[1]/"tools/grouping_bytecode.py")
grouping = importlib.util.module_from_spec(spec)
spec.loader.exec_module(grouping)
result = {"compiler":grouping.ES2ABC,"sha256":grouping.sha(grouping.ES2ABC),"comparisons":[]}
for name in ("self", "outer", "branches", "constructor_throw", "completed", "completed_throw", "completed_self", "reentry", "accessors", "member_nested", "member_throwing"):
    directory = output/name
    directory.mkdir(exist_ok=True)
    exported = directory/"export"
    subprocess.run([engine, str(fixtures/f"{name}.abc"), str(exported), "--value-source", "--language-source"], check=True, capture_output=True)
    row, = [r for r in json.loads((exported/"manifest.json").read_text())["records"] if r.get("source_status")==0]
    source = exported/row["source"]
    # SDK syntax adaptation: quote-free identifier export aliases on BOTH sides.
    # The source binding and external export name remain identical.
    adapted = directory/"emitted.js"
    adapted.write_text(re.sub(r' as "([A-Za-z_$][\w$]*)"',r' as \1',source.read_text()))
    original = directory/"original.js"
    original.write_text(re.sub(r' as "([A-Za-z_$][\w$]*)"',r' as \1',(fixtures/f"{name}.js").read_text()))
    for mode in ("default", "O0"):
        texts = []
        commands = []
        proofs = {}
        for side, js in (("original",original),("emitted",adapted)):
            abc = directory/f"{mode}-{side}.abc"
            pa = abc.with_suffix(".pa")
            command = [grouping.ES2ABC,"--module","--merge-abc",f"--record-name={source.stem}","--source-file=input.js","--extension=js",f"--output={abc}"]
            if mode=="O0": command.append("--opt-level=0")
            command.append(str(js));commands.append(command)
            assert grouping.run(command,abc.with_suffix(".compile.log"))==0
            assert grouping.run([grouping.DISASM,str(abc),str(pa)],abc.with_suffix(".disasm.log"))==0
            # Fresh decoding, raw SSA, independent scope/guard/cleanup and port
            # verification, including mutation and exact-budget checks.
            # This fixture exports the class and then reads the module binding. The
            # isolated unit creator adapter has no module-load resolution;
            # the production exporter above verifies its complete ports.
            arguments = ["--certificate-only"] if name == "completed_self" else []
            p = subprocess.run([probe,str(abc),*arguments],capture_output=True,text=True)
            abc.with_suffix(".proof.log").write_text(p.stdout+p.stderr)
            proofs[side] = {"layer":"raw_scope" if arguments else "raw_scope_and_ports", "status":"pass" if p.returncode==0 else "unverified", "returncode":p.returncode}
            if mode=="O0" and side=="original": assert p.returncode==0,(name,p.stdout,p.stderr)
            texts.append(grouping.assembly(pa))
        methods = [dict(re.findall(r"^(\.function [^\n]+)\n(.*?^})", text, re.M|re.S)) for text in texts]
        if mode=="O0":
            fixture_pa=directory/"fixture-original.pa"
            assert grouping.run([grouping.DISASM,str(fixtures/f"{name}.abc"),str(fixture_pa)],directory/"fixture-disasm.log")==0
            fixture_methods=dict(re.findall(r"^(\.function [^\n]+)\n(.*?^})",grouping.assembly(fixture_pa),re.M|re.S))
            assert fixture_methods==methods[0],(name,"original ABC method comparison")
        assert methods[0].keys()==methods[1].keys(), (name,mode,"method set")
        method_comparisons = [{"method":method,"identical":methods[0][method]==methods[1][method]} for method in methods[0]]
        different = texts[0]!=texts[1]
        (directory/f"{mode}.diff").write_text("".join(difflib.unified_diff(texts[0].splitlines(True),texts[1].splitlines(True),fromfile="original",tofile="emitted")))
        result["comparisons"].append({"fixture":name,"mode":mode,"identical":not different,"raw_port_verification":proofs,"methods":method_comparisons,"commands":commands})
(output/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
print(json.dumps({"comparisons":len(result["comparisons"]),"identical":sum(r["identical"] for r in result["comparisons"]),"different":sum(not r["identical"] for r in result["comparisons"]),"raw_port_verification":{status:sum(p["status"]==status for r in result["comparisons"] for p in r["raw_port_verification"].values()) for status in ("pass","unverified")},"sha256":result["sha256"]}))
