#!/usr/bin/env python3
"""Historical script probes: emission, V9 publication gates and old-contract refusals."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tests/support/isa'))
from oracle_evidence import sha
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/globals'
class Globals(unittest.TestCase):
 def test_script_emission(self):
  registry=json.loads((FIX/'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,e in enumerate(registry['fixtures']):
    with self.subTest(fixture=e['path']):
     abc=FIX/e['path'];source=FIX/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256']);self.assertNotIn('--module',e['command'])
     cmd=[('--output='+str(root/'check.abc')) if a.startswith('--output=') else a for a in e['command']];subprocess.run(cmd,check=True,cwd=ROOT,capture_output=True);self.assertEqual(sha(root/'check.abc'),e['sha256'])
     out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True);records=json.loads((out/'manifest.json').read_text())['records'];script=next(r for r in records if r.get('source_language')=='javascript-script');self.assertEqual(script['source_status'],0 if source.stem in ('sloppy','script','wide') else 3);self.assertTrue(all(r['source_status']==2 for r in records if r is not script))
     rows=[r for line in subprocess.check_output([BUILD/'isa_coverage_probe',abc],text=True,stderr=subprocess.DEVNULL).splitlines() if (r:=json.loads(line))['type']=='instruction'];self.assertTrue(set(e['opcodes']) <= {r['encoding']['opcode'] for r in rows})
 def test_standard_script_observations(self):
  result=subprocess.check_output(['node','-e',"const vm=require('node:vm'),fs=require('node:fs');let a={};vm.runInNewContext(fs.readFileSync(process.argv[1],'utf8'),a);let b={};vm.runInNewContext(fs.readFileSync(process.argv[2],'utf8'),b);console.log(JSON.stringify([a.observation,b.missing]));",FIX/'tdz.js',FIX/'sloppy.js'],text=True);self.assertEqual(json.loads(result),['ReferenceError',4])
if __name__=='__main__':unittest.main()
