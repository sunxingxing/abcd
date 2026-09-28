import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { checkQueryDegree, checkRecover } from './exception_family_execution.mjs';

// Discovery must be available before recursive child evaluation; it does not
// authorize a parent body, invent child bindings, or close capture obligations.
assert.equal(createHash('sha256').update(readFileSync(process.argv[3])).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
const PhaseError = Object.freeze({ Evaluation: 7 }); // C++ PhasedMethodError boundary.
const samples = [
  { method: 8257, sites: [{ instruction: 38, operation: 38, pc: 95 }, { instruction: 46, operation: 46, pc: 118 }], targets: [7810, 8118], allocations: 0, bindings: 0, captures: [0, 0] },
  { method: 8404, sites: [{ instruction: 20, operation: 20, pc: 52 }], targets: [7978], allocations: 1, bindings: 1, captures: [1] },
];
for (const { method, sites, targets, allocations, bindings, captures } of samples) {
  const result = JSON.parse(execFileSync(process.argv[2], [process.argv[3], String(method)], { encoding: 'utf8' }));
  assert.equal(result.phase_body_ready, true);
  assert.equal(result.owned_phase_input, true);
  assert.equal(result.phase_environments_ready, true);
  assert.equal(result.phase_cells_ready, true);
  assert.equal(result.conditional_evaluation_ready, true);
  assert.equal(result.conditional_parent_tree_ready, true);
  assert.equal(result.conditional_child_trees, targets.length);
  assert.equal(result.conditional_family_layout_ready, true);
  assert.equal(result.conditional_family_enclosure_ready, true);
  assert.equal(result.family_writes_ready, true);
  assert.equal(result.family_write_creations, targets.length);
  assert.equal(result.rejected_family_write_forgery, true);
  assert.equal(result.family_abi_methods, targets.length + 1);
  assert.equal(result.family_source_methods, targets.length + 1);
  assert.equal(result.rejected_source_forgery, true);
  assert.equal(result.family_mapped_instructions, method === 8404 ? 90 : 145);
  assert.equal(typeof result.conditional_family_source, 'string');
  (method === 8404 ? checkQueryDegree : checkRecover)(result.conditional_family_source);
  assert.equal(result.checked_store_methods, targets.length + 1);
  assert.equal(result.checked_lexical_stores, method === 8404 ? 1 : 0);
  assert.equal(result.rejected_capture_forgery, captures.some(count => count !== 0));
  assert.equal(result.cells, bindings);
  assert.equal(result.environment_allocations, allocations);
  assert.equal(result.lexical_bindings, bindings);
  assert.deepEqual(result.creations.map(({ operation, pc, instruction }) => ({ operation, pc, instruction })), sites);
  assert.deepEqual(result.creations.map(site => site.target), targets);
  assert.deepEqual(result.creations.map(site => site.capture_occurrences), captures);
  assert.equal(result.source_complete, false);
  assert.equal(result.executable, false);
  assert.equal(result.phase_error, PhaseError.Evaluation);
  for (const site of result.creations) {
    assert.equal(site.capture_inputs_ready, true);
    assert.equal(site.capture_cells_ready, true);
    assert.equal(site.capture_evaluation_ready, true);
    assert.equal(site.parent_cell_targets, method === 8404 ? 1 : 0);
    assert.equal(site.conditional_writes, true);
    assert(Number.isInteger(site.target));
    assert(Number.isInteger(site.operation));
    assert(Number.isInteger(site.pc));
    assert(Number.isInteger(site.instruction));
  }
}
