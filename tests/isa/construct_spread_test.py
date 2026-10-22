#!/usr/bin/env python3
"""Private argument-array extraction for NEWOBJAPPLY, never arbitrary host arrays."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tests/support/isa'))
from oracle_evidence import sha,record_node
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/construct_spread'
class Constructs(unittest.TestCase):
 def test_sources(self):
  registry=json.loads((FIX/'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as d:
   scratch=Path(d)
   for i,e in enumerate(registry['fixtures']):
    with self.subTest(fixture=e['path']):
     abc=ROOT/e['path'];original=ROOT/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(original),e['source_sha256'])
     export=scratch/str(i);subprocess.run([BUILD/'abc_structure',abc,export,'--value-source','--language-source'],check=True,capture_output=True)
     manifest=json.loads((export/'manifest.json').read_text());record=manifest['records'][-1]
     if e.get('refusal'):
      self.assertNotEqual(record['source_status'],0);self.assertEqual(record['source_failure']['causes'],e['refusal']);continue
     self.assertEqual(record['source_status'],0,record)
     emitted=export/record['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
     observed=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/construct_spread.mjs',original,emitted],text=True))
     record_node(abc,export,original,__file__,observed['cases'],BUILD/'abc_structure',exclusions=['array-spread-v1'],dependencies=[ROOT/'tests/integration/construct_spread.mjs'])
 def test_scale(self):
  compiler=json.loads((FIX/'manifest.json').read_text())['compiler']
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for count in (200,201,400,800):
    source=root/(str(count)+'.js');abc=source.with_suffix('.abc')
    source.write_text('export function run(xs){ return new (function C(...args){this.args=args;})('+','.join(['...xs']*count)+'); }')
    subprocess.run([compiler,'--module','--merge-abc','--extension=js','--record-name=scaleConstruct','--output='+str(abc),str(source)],check=True,capture_output=True)
    export=root/(str(count)+'-out');subprocess.run([BUILD/'abc_structure',abc,export,'--value-source','--language-source'],check=True,capture_output=True)
    record=json.loads((export/'manifest.json').read_text())['records'][-1];self.assertEqual(record['source_status'],0,record)
    emitted=export/record['source'];observed=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/construct_spread.mjs',source,emitted],text=True));self.assertGreater(observed['cases'],0)
if __name__=='__main__':unittest.main()
