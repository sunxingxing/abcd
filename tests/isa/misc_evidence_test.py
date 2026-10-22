#!/usr/bin/env python3
"""Measured SDK emitter/refusal boundaries, never a successful semantic oracle."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tests/support/isa'))
from oracle_evidence import sha
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/misc_evidence'
class Evidence(unittest.TestCase):
 def test_emitters(self):
  manifest=json.loads((FIX/'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,e in enumerate(manifest['fixtures']):
    with self.subTest(fixture=e['path']):
     abc=ROOT/e['path'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(ROOT/e['source']),e['source_sha256'])
     command=[('--output='+str(root/'check.abc')) if x.startswith('--output=') else x for x in e['command']]
     subprocess.run(command,cwd=ROOT,check=True,capture_output=True);self.assertEqual(sha(root/'check.abc'),e['sha256'])
     subprocess.run([manifest['disassembler'],abc,root/'check.pa'],check=True,capture_output=True)
     rows=[json.loads(line) for line in subprocess.check_output([BUILD/'isa_coverage_probe',abc],text=True,stderr=subprocess.DEVNULL).splitlines()];self.assertEqual(set(e['opcodes']),{r['encoding']['opcode'] for r in rows if r['type']=='instruction'})
     out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True)
     record=json.loads((out/'manifest.json').read_text())['records'][-1];self.assertEqual(record['source_status'],e['source_status']);self.assertEqual(record.get('source_failure',{}).get('causes',[]),e['causes'])
 def test_standard_side_boundaries(self):
  # These distinguish native JS behavior only, never execute or attest Ark.
  subprocess.run(['node','--input-type=module','-e',r"""
import assert from 'node:assert/strict';
const trace=[];const token={};const promise=import({toString(){trace.push('coerce');throw token;}});
trace.push('after');await promise.catch(e=>{assert.equal(e,token);trace.push('reject');});
assert.deepEqual(trace,['coerce','after','reject']);
const saved=Symbol;try{globalThis.Symbol=function Replacement(){};assert.notEqual(Symbol,saved);}finally{globalThis.Symbol=saved;}
const key=7;assert.equal(typeof ({[key]:function(){}})[7].name,'string');
const reads=[];const invalid={ [Symbol.iterator]:{get next(){reads.push('next');return ()=>({done:true});}} };
assert.throws(()=>{const [x]=invalid;},TypeError);assert.deepEqual(reads,[]);
"""],check=True,capture_output=True)
if __name__=='__main__':unittest.main()
