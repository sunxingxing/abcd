#!/usr/bin/env python3
"""Real compiler, geometric lexical-cell scale; assert charged work, not clocks."""
import json
import math
from pathlib import Path
import subprocess
import tempfile
import time
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/regression'
COMPILER = json.loads((ROOT / 'tests/fixtures/isa/environment_renewal/manifest.json').read_text())['compiler']
rows = []
with tempfile.TemporaryDirectory() as temporary:
 directory = Path(temporary)
 for count in (100, 200, 201, 400, 800):
  source, abc = directory / 'scale.js', directory / 'scale.abc'
  declarations = ''.join(f'let x{i} = {i};' for i in range(count))
  captures = ','.join(f'x{i}' for i in range(count))
  source.write_text(f'export async function scale(keep){{for(let i=0;i<3;i++){{{declarations}keep(()=>[{captures}]);}}await 0;}}\n')
  subprocess.run([COMPILER,'--module','--merge-abc','--extension=js','--record-name=renewal_scale','--source-file=scale.js','--output='+str(abc),'--opt-level=0',str(source)],check=True,capture_output=True)
  start = time.monotonic()
  output = subprocess.check_output([BUILD / 'environment_renewal_test',abc],text=True)
  elapsed = time.monotonic() - start
  records = [dict(item.split('=',1) for item in line.split()) for line in output.splitlines() if 'work=' in line]
  assert len(records) == 1, output
  record = records[0]
  assert int(record['transfers']) == count, record
  work = int(record['work'])
  rows.append({'cells':count,'work':work,'seconds':round(elapsed,4),'work_per_n_log_n':round(work/(count*math.log2(count+1)),3)})
 assert max(r['work_per_n_log_n'] for r in rows) <= 1.5 * rows[0]['work_per_n_log_n'], rows
 assert rows[-1]['work'] < 12 * rows[0]['work'], rows
print(json.dumps({'cases':len(rows),'rows':rows}))
