#!/usr/bin/env python3
"""Composition of owner-certified awaits/yields and array-pattern folding."""
import json, subprocess, sys, tempfile, unittest
from pathlib import Path
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/support/isa'))
from oracle_evidence import sha, record_node
BUILD = ROOT / 'build/regression'
FIX = ROOT / 'tests/fixtures/isa/normalized_protocols'
HARNESS = ROOT / 'tests/integration/normalized_protocols.mjs'

class NormalizedProtocols(unittest.TestCase):
 def test_sources(self):
  manifest = json.loads((FIX / 'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as d:
   for i, entry in enumerate(manifest['fixtures']):
    with self.subTest(kind=entry['kind']):
     abc, source = ROOT / entry['path'], ROOT / entry['source']
     self.assertEqual(sha(abc), entry['sha256'])
     self.assertEqual(sha(source), entry['source_sha256'])
     out = Path(d) / str(i)
     subprocess.run([BUILD / 'abc_structure', abc, out, '--value-source', '--language-source'], check=True, capture_output=True)
     records = json.loads((out / 'manifest.json').read_text())['records']
     record = records[-1]
     if 'refusal' in entry:
      self.assertEqual(record['source_status'], 3, record)
      self.assertIn(entry['refusal'], record['source_failure']['causes'])
      self.assertNotIn('source', record)
      continue
     self.assertEqual(record['source_status'], 0, record)
     emitted = out / record['source']
     text = emitted.read_text()
     self.assertIn('[', text)
     self.assertNotIn('Symbol.iterator', text)
     subprocess.run(['node', '--check', emitted], check=True, capture_output=True)
     observed = json.loads(subprocess.check_output(['node', HARNESS, source, emitted, entry['kind']], text=True))
     self.assertGreaterEqual(observed['cases'], 20)
     record_node(abc, out, source, __file__, observed['cases'], BUILD / 'abc_structure', dependencies=[HARNESS])

if __name__ == '__main__': unittest.main()
