import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';

const [tool, abc] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
// Real ABC-only class entries previously refused at DEFINECLASSWITHBUFFER.
// This verifies imported values, not class-body emission or executable semantics.
for (const entry of [7312, 7531, 16638, 29031]) {
  const run = spawnSync(tool, [abc, String(entry), '--conditional-values'],
    {encoding: 'utf8', timeout: 60_000});
  assert.equal(run.status, 1, run.stderr + run.stdout);
  const report = JSON.parse(run.stdout);
  assert.equal(report.method, entry);
  assert.equal(report.values_prepared, true);
  assert.equal(report.module_context_ready, true);
  assert.ok(report.operations > 0);
  assert.ok(report.child_obligations > 0);
  assert.equal(report.class_heritage.length, 1);
  assert.equal(report.class_heritage[0].kind, 'absent');
  assert.equal(report.class_heritage[0].pc, 9);
  assert.equal(report.source_complete, false);
  assert.equal(report.executable, false);
}
