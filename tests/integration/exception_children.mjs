import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';

// Discovery must be available before recursive child evaluation; it does not
// authorize a parent body, invent child bindings, or close capture obligations.
assert.equal(createHash('sha256').update(readFileSync(process.argv[3])).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
const PhaseError = Object.freeze({ Evaluation: 7 }); // C++ PhasedMethodError boundary.
const samples = [
  { method: 8257, sites: [{ instruction: 38, operation: 38, pc: 95 }, { instruction: 46, operation: 46, pc: 118 }], targets: [7810, 8118], allocations: 0, bindings: 0 },
  { method: 8404, sites: [{ instruction: 20, operation: 20, pc: 52 }], targets: [7978], allocations: 1, bindings: 1 },
];
for (const { method, sites, targets, allocations, bindings } of samples) {
  const result = JSON.parse(execFileSync(process.argv[2], [process.argv[3], String(method)], { encoding: 'utf8' }));
  assert.equal(result.phase_body_ready, true);
  assert.equal(result.phase_environments_ready, true);
  assert.equal(result.phase_cells_ready, true);
  assert.equal(result.cells, bindings);
  assert.equal(result.environment_allocations, allocations);
  assert.equal(result.lexical_bindings, bindings);
  assert.deepEqual(result.creations.map(({ operation, pc, instruction }) => ({ operation, pc, instruction })), sites);
  assert.deepEqual(result.creations.map(site => site.target), targets);
  assert.equal(result.source_complete, false);
  assert.equal(result.executable, false);
  assert.equal(result.phase_error, PhaseError.Evaluation);
  for (const site of result.creations) {
    assert(Number.isInteger(site.target));
    assert(Number.isInteger(site.operation));
    assert(Number.isInteger(site.pc));
    assert(Number.isInteger(site.instruction));
  }
}
