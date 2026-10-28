#!/usr/bin/env python3
"""Geometric growth of the same certified CFG; includes the 200/201 boundary."""
import json
from pathlib import Path
import re
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[2]
class Scaling(unittest.TestCase):
    def test_growth(self):
        rows=[]
        for padding in (100, 200, 201, 400, 800):
            text=subprocess.check_output([ROOT/'build/regression/iterator_continuation_test',
                ROOT/'tests/fixtures/isa/iterator_continuation/choose-O0.abc', 'choose', str(padding)],text=True)
            row={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',text)}
            self.assertEqual(row['continuations'],1)
            rows.append(row)
        for a,b in zip(rows,rows[1:]):
            # A sparse constant-domain query plus shared sorting/indexing.
            ratio=b['nodes']/a['nodes']
            self.assertLessEqual(b['work'],a['work']*max(1.1,ratio*1.35))
            self.assertLessEqual(b['bytes'],a['bytes']*max(1.1,ratio*1.15))
        print(json.dumps(rows))
if __name__=='__main__': unittest.main()
