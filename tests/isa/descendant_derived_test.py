#!/usr/bin/env python3
"""Derived classes created by descendant and phased creators."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/support/isa'))
from oracle_evidence import sha, record_node
BUILD = ROOT / 'build/regression'
FIX = ROOT / 'tests/fixtures/isa/descendant_derived'
HARNESS = ROOT / 'tests/integration/descendant_derived.mjs'

class DescendantDerived(unittest.TestCase):
 def test_sources(self):
  manifest = json.loads((FIX / 'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as temporary:
   for i, fixture in enumerate(manifest['fixtures']):
    abc, source = ROOT / fixture['path'], ROOT / fixture['source']
    self.assertEqual(sha(abc), fixture['sha256'])
    self.assertEqual(sha(source), fixture['source_sha256'])
    out = Path(temporary) / str(i)
    subprocess.run([BUILD / 'abc_structure', abc, out, '--value-source'], check=True, capture_output=True)
    record = json.loads((out / 'manifest.json').read_text())['records'][-1]
    self.assertEqual(record['source_status'], 0, record)
    emitted = out / record['source']
    subprocess.run(['node', '--check', emitted], check=True, capture_output=True)
    observed = json.loads(subprocess.check_output(['node', HARNESS, source, emitted], text=True))
    self.assertGreaterEqual(observed['cases'], 4)
    record_node(abc, out, source, __file__, observed['cases'], BUILD / 'abc_structure', dependencies=[HARNESS])

if __name__ == '__main__': unittest.main()
