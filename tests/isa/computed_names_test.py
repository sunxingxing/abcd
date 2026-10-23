#!/usr/bin/env python3
"""V10 private computed own definitions: source, order, identity and oracle."""
import json, subprocess, sys, tempfile, unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tests/support/isa'))
from oracle_evidence import sha,record_node
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/computed_names'
class Names(unittest.TestCase):
 def test_sources(self):
  manifest=json.loads((FIX/'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,e in enumerate(manifest['fixtures']):
    with self.subTest(fixture=e['path']):
     abc=ROOT/e['path'];source=ROOT/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
     out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True)
     record=json.loads((out/'manifest.json').read_text())['records'][-1];self.assertEqual(record['source_status'],0,record)
     emitted=out/record['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
     observed=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/computed_names.mjs',source,emitted],text=True))
     record_node(abc,out,source,__file__,observed['cases'],BUILD/'abc_structure',exclusions=observed['exclusions'],dependencies=[ROOT/'tests/integration/computed_names.mjs'])
     previous=root/(str(i)+'-previous');subprocess.run([BUILD/'abc_structure',abc,previous,'--value-source','--language-numeric','--language-constructors'],check=True,capture_output=True)
     old=json.loads((previous/'manifest.json').read_text())['records'][-1];self.assertNotEqual(old['source_status'],0)
     self.assertIn({'domain':'abcd::source::LiteralError','value':10},old['source_failure']['causes'])
 def test_scale(self):
  manifest=json.loads((FIX/'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for n in (200,201,400,800):
    source=root/(str(n)+'.js');abc=source.with_suffix('.abc')
    source.write_text('export function make(k,trace){return {[k + '+json.dumps('s'*n)+']: function(){return 17;}};}')
    subprocess.run([manifest['compiler'],'--module','--merge-abc','--extension=js','--record-name=scaleNames','--output='+str(abc),source],check=True,capture_output=True)
    out=root/(str(n)+'-out');subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True)
    record=json.loads((out/'manifest.json').read_text())['records'][-1];self.assertEqual(record['source_status'],0,record)
    subprocess.run(['node','--check',out/record['source']],check=True,capture_output=True)
    subprocess.run(['node',ROOT/'tests/integration/computed_names.mjs',source,out/record['source']],check=True,capture_output=True)
if __name__=='__main__':unittest.main()
