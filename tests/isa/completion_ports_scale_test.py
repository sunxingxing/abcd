#!/usr/bin/env python3
"""Owned-body invariance and geometric completion-port proof admission."""
import json
import math
from pathlib import Path
import re
import subprocess
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / 'build/regression/completion_ports_test'
FIXTURE = ROOT / 'tests/fixtures/isa/completion_ports/ports-O0.abc'

class CompletionPortScale(unittest.TestCase):
 def test_growth_and_invariance(self):
  rows = []
  def run(*args):
   start = time.monotonic()
   result = subprocess.run([str(BINARY), *map(str, args)], capture_output=True, text=True, check=True)
   match = re.search(r'^method=(\d+) entries=(\d+) exits=(\d+) resumes=(\d+) work=(\d+) bytes=(\d+) nodes=(\d+) qualified=(\d+)$', result.stdout, re.M)
   self.assertIsNotNone(match, result.stdout)
   row = dict(zip(('method','entries','exits','resumes','work','bytes','nodes','qualified'), map(int, match.groups())))
   row['seconds'] = round(time.monotonic() - start, 4)
   return row
  base = run()
  for count in (100, 200, 201, 400, 800):
   row = run(FIXTURE, base['method'], count)
   row['padding'] = count
   for field in ('entries','exits','resumes','qualified'):
    self.assertEqual(row[field], base[field], (field, row, base))
   # Failed legacy-control work grants are included. This is charged work,
   # not a noisy timing threshold or an allocator-only peak-RSS claim.
   bound = 4 * base['work'] * row['nodes'] * math.log2(row['nodes'] + 1) / (base['nodes'] * math.log2(base['nodes'] + 1))
   self.assertLessEqual(row['work'], bound, row)
   rows.append(row)
  for variant in ('registers', 'copies', 'split'):
   row = run(FIXTURE, base['method'], 0, variant)
   self.assertEqual(row['entries'], base['entries'], (variant,row))
   row['variant'] = variant
   rows.append(row)
  print(json.dumps({'base':base,'measurements':rows}, sort_keys=True))

if __name__ == '__main__': unittest.main()
