#!/usr/bin/env python3
"""Node differential for edge-qualified compiler close rethrows."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/support/isa'))
from oracle_evidence import sha, record_node
BUILD = ROOT / 'build/regression'
FIX = ROOT / 'tests/fixtures/isa/guarded_catch_payload'
HARNESS = ROOT / 'tests/integration/guarded_catch_payload.mjs'

class GuardedCatchPayload(unittest.TestCase):
    def test_sources(self):
        manifest = json.loads((FIX / 'manifest.json').read_text())
        with tempfile.TemporaryDirectory() as directory:
            for index, entry in enumerate(manifest['fixtures']):
                abc, source = ROOT / entry['path'], ROOT / entry['source']
                self.assertEqual(sha(abc), entry['sha256'])
                self.assertEqual(sha(source), entry['source_sha256'])
                out = Path(directory) / str(index)
                subprocess.run([BUILD / 'abc_structure', abc, out, '--value-source', '--language-source'],
                               check=True, capture_output=True)
                record = json.loads((out / 'manifest.json').read_text())['records'][-1]
                if 'refusal' in entry:
                    self.assertEqual(record['source_status'], 3, record)
                    self.assertEqual(record['source_failure']['causes'], entry['refusal'])
                    self.assertNotIn('source', record)
                    continue
                self.assertEqual(record['source_status'], 0, record)
                emitted = out / record['source']
                subprocess.run(['node', '--check', emitted], check=True, capture_output=True)
                observed = json.loads(subprocess.check_output(['node', HARNESS, source, emitted], text=True))
                self.assertGreaterEqual(observed['cases'], 40)
                record_node(abc, out, source, __file__, observed['cases'], BUILD / 'abc_structure', dependencies=[HARNESS])

if __name__ == '__main__':
    unittest.main()
