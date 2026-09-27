// Specification only during the testing pause. No generated JS is executed.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';

const [tool, abc] = process.argv.slice(2);
function inspect(method) {
  const run = spawnSync(tool, [abc, String(method), '--value-source'], {
    encoding: 'utf8', maxBuffer: 8 * 1024 * 1024,
  });
  assert.equal(run.error, undefined);
  assert.equal(run.signal, null);
  assert.equal(run.status, 1);
  return JSON.parse(run.stdout);
}
for (const method of [7117, 7477]) {
  const result = inspect(method);
  assert.equal(result.fragment, 'function_expression');
  assert.equal(result.required_strict_destination, true);
  assert.equal(result.source_complete, false);
  assert.equal(result.executable, false);
  assert.equal(result.method_slice_complete, true);
  assert.equal(result.route_receipt.method, method);
  assert.equal(result.route_receipt.record, 6897);
  assert.equal(result.route_receipt.stage, 'printed');
  assert.equal(result.route_receipt.coverage, 'complete');
  assert.equal(result.route_receipt.legacy_writer_used, false);
  assert.equal(result.family_members.length, 1);
  const member = result.family_members[0];
  assert.deepEqual(member.route_receipt, result.route_receipt);
  assert.deepEqual(member.local_positions, result.positions);
  assert.ok(member.begin >= 0 && member.begin < member.end);
  assert.ok(member.end <= Buffer.byteLength(result.source));
  assert.equal(result.positions.instruction_sources.length, 5);
  for (const [site, source] of result.positions.instruction_sources.entries()) {
    assert.equal(result.positions.origins[source.origin].site, site);
    if (source.kind === 'elided_pure') assert.equal(source.range, null);
    else {
      assert.ok(source.range.begin >= member.begin);
      assert.ok(source.range.begin <= source.range.end);
      assert.ok(source.range.end <= member.end);
    }
  }
}
// Family refusal must not manufacture completed root stages or hide behind an
// invalid serialized receipt. This closure needs external embedding evidence.
const refused = inspect(7449);
assert.equal(refused.source, undefined);
assert.equal(refused.receipt_error, undefined);
assert.equal(refused.route_receipt.stage, 'decoded');
assert.equal(refused.route_receipt.coverage, 'unsupported');
assert.equal(refused.route_receipt.failure.reason, 'context_stage');
assert.equal(refused.route_receipt.failure.pc, null);
assert.ok(refused.family_failure);
