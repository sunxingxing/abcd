import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { checkQueryDegree, checkRecover } from './exception_family_execution.mjs';

const [tool, sample] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(sample)).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
for (const [method, methods, mapped, execute] of [
  [8257, [8257, 7810, 8118], 145, checkRecover],
  [8404, [8404, 7978], 90, checkQueryDegree],
]) {
  const invoke = (mode) => {
    const result = spawnSync(tool, [sample, String(method), mode], { encoding: 'utf8' });
    assert.equal(result.status, 1, result.stderr);
    const report = JSON.parse(result.stdout);
    assert.equal(report.source_complete, false);
    assert.equal(report.executable, false);
    return report;
  };
  const report = invoke('--family-source-draft');
  assert.equal(report.family_root_context, true);
  assert.equal(report.family_prepared, true);
  assert.equal(report.family_tree_prepared, true);
  assert.equal(report.draft_prepared, true);
  assert.deepEqual(report.draft_positions.map(owner => owner.method), methods);
  assert.equal(report.family_writes.creations, methods.length - 1);
  let count = 0;
  const bytes = Buffer.byteLength(report.draft_source);
  for (const owner of report.draft_positions) {
    assert(0 <= owner.begin && owner.begin < owner.end && owner.end <= bytes);
    const positions = owner.positions;
    for (const source of positions.instruction_sources) {
      const origin = positions.origins[source.origin];
      assert.equal(origin.site, source.site);
      if (source.range !== null) {
        assert(owner.begin <= source.range.begin && source.range.begin <= source.range.end);
        assert(source.range.end <= owner.end);
      }
      ++count;
    }
  }
  assert.equal(count, mapped);
  execute(report.draft_source);
  const diagnostic = invoke('--conditional-family');
  assert.equal(diagnostic.family_tree_prepared, true);
  assert.equal(Object.hasOwn(diagnostic, 'draft_source'), false);
  assert.equal(Object.hasOwn(diagnostic, 'draft_positions'), false);
}
