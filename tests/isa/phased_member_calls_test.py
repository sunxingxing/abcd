#!/usr/bin/env python3
"""Phased member References with selections in complete argument subtrees."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tests/support/isa'))
from oracle_evidence import sha,record_node
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/phased_member_calls'
HARNESS=ROOT/'tests/integration/phased_member_calls.mjs'
class PhasedMemberCalls(unittest.TestCase):
 def test_sources(self):
  manifest=json.loads((FIX/'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,e in enumerate(manifest['fixtures']):
    abc=ROOT/e['path'];source=ROOT/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
    out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True)
    record=json.loads((out/'manifest.json').read_text())['records'][-1]
    if e.get('refusal'):
     self.assertEqual(record['source_status'],3,record)
     self.assertEqual(record['source_failure']['causes'],e['refusal'])
     self.assertEqual(record['source_failure']['call_reference']['kind'],1)
     self.assertNotIn('source',record)
     continue
    self.assertEqual(record['source_status'],0,record)
    emitted=out/record['source']
    subprocess.run(['node','--check',emitted],check=True,capture_output=True)
    observed=json.loads(subprocess.check_output(['node',HARNESS,source,emitted],text=True))
    self.assertGreaterEqual(observed['cases'],20)
    record_node(abc,out,source,__file__,observed['cases'],BUILD/'abc_structure',dependencies=[HARNESS])
if __name__=='__main__':unittest.main()
