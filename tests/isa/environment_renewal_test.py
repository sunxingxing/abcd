#!/usr/bin/env python3
"""Certified body-reset cells, including guarded cleanup and finally payloads."""
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
FIX = ROOT / 'tests/fixtures/isa/environment_renewal'
HARNESS = ROOT / 'tests/integration/environment_renewal.mjs'

class EnvironmentRenewal(unittest.TestCase):
 def test_sources(self):
  manifest = json.loads((FIX / 'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as temporary:
   for i, fixture in enumerate(manifest['fixtures']):
    with self.subTest(kind=fixture['kind']):
     abc, source = ROOT / fixture['path'], ROOT / fixture['source']
     self.assertEqual(sha(abc), fixture['sha256'])
     self.assertEqual(sha(source), fixture['source_sha256'])
     out = Path(temporary) / str(i)
     subprocess.run([BUILD / 'abc_structure', abc, out, '--value-source'], check=True, capture_output=True)
     record = json.loads((out / 'manifest.json').read_text())['records'][-1]
     if fixture['kind'] == 'header':
      self.assertEqual(record['source_status'], 3, record)
      causes = record['source_failure']['causes']
      if fixture['kind'] == 'header':
       for cause in ({'domain':'abcd::source::PhasedMethodError','value':2},
                     {'domain':'abcd::analysis::EnvironmentFlowError','value':4},
                     {'domain':'abcd::analysis::EnvironmentRenewalError','value':11}):
        self.assertIn(cause, causes)
       self.assertIn('pc', record['source_failure'])
      self.assertNotIn('source', record)
      continue
     self.assertEqual(record['source_status'], 0, record)
     emitted = out / record['source']
     self.assertNotIn('({});', emitted.read_text())
     subprocess.run(['node', '--check', emitted], check=True, capture_output=True)
     observed = json.loads(subprocess.check_output(['node', HARNESS, source, emitted, fixture['kind']], text=True))
     self.assertGreaterEqual(observed['cases'], 20)
     record_node(abc, out, source, __file__, observed['cases'], BUILD / 'abc_structure', dependencies=[HARNESS])

if __name__ == '__main__': unittest.main()
