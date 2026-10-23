#!/usr/bin/env python3
"""arguments in async functions, with a Node oracle."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tests/support/isa'))
from oracle_evidence import sha,record_node
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/async_arguments'
HARNESS=ROOT/'tests/integration/async_arguments.mjs'
class AsyncArguments(unittest.TestCase):
 def test_sources(self):
  manifest=json.loads((FIX/'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,e in enumerate(manifest['fixtures']):
    abc=ROOT/e['path'];source=ROOT/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
    out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True)
    record=json.loads((out/'manifest.json').read_text())['records'][-1];self.assertEqual(record['source_status'],0,record)
    emitted=out/record['source'];text=emitted.read_text()
    subprocess.run(['node','--check',emitted],check=True,capture_output=True)
    self.assertIn('arguments',text)
    observed=json.loads(subprocess.check_output(['node',HARNESS,source,emitted],text=True))
    self.assertGreaterEqual(observed['cases'],4)
    record_node(abc,out,source,__file__,observed['cases'],BUILD/'abc_structure',dependencies=[HARNESS])
if __name__=='__main__':unittest.main()
