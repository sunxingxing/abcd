#!/usr/bin/env python3
"""Native nested iterator completion routes, with independent Node evidence."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tests/support/isa'))
from oracle_evidence import sha, record_node
BUILD=ROOT/'build/regression'
FIX=ROOT/'tests/fixtures/isa/completion_ports'
HARNESS=ROOT/'tests/integration/completion_ports.mjs'

class CompletionPorts(unittest.TestCase):
 def test_sources(self):
  manifest=json.loads((FIX/'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as temporary:
   for i,fixture in enumerate(manifest['fixtures']):
    abc,source=ROOT/fixture['path'],ROOT/fixture['source']
    self.assertEqual(sha(abc),fixture['sha256'])
    self.assertEqual(sha(source),fixture['source_sha256'])
    out=Path(temporary)/str(i)
    subprocess.run([BUILD/'abc_structure',abc,out,'--value-source'],check=True,capture_output=True)
    manifest_out=json.loads((out/'manifest.json').read_text())
    self.assertEqual(manifest_out['source_contract'],'language_source_v10')
    record=manifest_out['records'][-1]
    if 'refusal' in fixture:
     self.assertEqual(record['source_status'],3,record)
     self.assertIn(fixture['refusal'],record['source_failure']['causes'])
     self.assertNotIn('source',record)
     continue
    self.assertEqual(record['source_status'],0,record)
    emitted=out/record['source']
    text=emitted.read_text()
    self.assertNotRegex(text, r'\.\s*return\s*\(')
    self.assertIn(' of ', text)
    subprocess.run(['node','--check',emitted],check=True,capture_output=True)
    observed=json.loads(subprocess.check_output(['node',HARNESS,source,emitted],text=True))
    self.assertGreaterEqual(observed['cases'],200)
    record_node(abc,out,source,__file__,observed['cases'],BUILD/'abc_structure',dependencies=[HARNESS])

if __name__=='__main__':unittest.main()
