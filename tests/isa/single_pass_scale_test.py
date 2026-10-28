#!/usr/bin/env python3
"""Single-pass scope proof: shared indexes, fixed K, 200/201 and geometric N."""
import json
from pathlib import Path
import re
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[2]
class Scaling(unittest.TestCase):
 def test_growth(self):
  rows=[]
  for padding in (100,200,201,400,800):
   text=subprocess.check_output([ROOT/'build/regression/single_pass_test',ROOT/'tests/fixtures/isa/single_pass/once-O0.abc','onceSync',str(padding)],text=True)
   row={k:int(v)for k,v in re.findall(r'(\w+)=(\d+)',text.splitlines()[-1])};self.assertEqual(row['scopes'],1);rows.append(row)
  for a,b in zip(rows,rows[1:]):
   ratio=b['nodes']/a['nodes'];self.assertLessEqual(b['work'],a['work']*max(1.1,ratio*1.4));self.assertLessEqual(b['bytes'],a['bytes']*max(1.1,ratio*1.15))
  print(json.dumps(rows))
if __name__=='__main__':unittest.main()
