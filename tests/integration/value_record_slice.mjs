// T03 record assembly regression specification, unrun during the test pause.
// Only ABC is passed to the engine; no sidecar source or source-map input.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';

const [tool, abc, methodText, hash] = process.argv.slice(2);
function inspect(option) {
  const child = spawnSync(tool, [abc, methodText, option], {
    encoding: 'utf8', maxBuffer: 4 * 1024 * 1024,
  });
  assert.equal(child.error, undefined);
  assert.equal(child.signal, null);
  assert.equal(child.status, 1); // Neither view certifies a complete record.
  return JSON.parse(child.stdout);
}
const member = inspect('--value-source');
const record = inspect('--record-value-slice');
assert.equal(record.fragment, 'record_slice');
assert.equal(record.record_emitted, true);
assert.equal(record.enclosing_route, 'legacy');
assert.equal(record.source_complete, false);
assert.equal(record.executable, false);
assert.equal(record.function_segments.length, 1);
const segment = record.function_segments[0];
assert.equal(segment.route_receipt.identity.sha256, hash);
assert.equal(segment.route_receipt.method, Number(methodText));
assert.equal(segment.route_receipt.route, 'value_tree');
assert.equal(segment.route_receipt.legacy_writer_used, false);
assert.equal(segment.route_receipt.coverage, 'complete');
assert.deepEqual(segment.route_receipt, member.route_receipt);
const { instruction_sources, ...rootPositions } = segment.local_positions;
assert.deepEqual(rootPositions, member.positions);
assert.equal(segment.family_members.length, 1);
assert.deepEqual(segment.family_members[0].route_receipt, segment.route_receipt);
assert.deepEqual(segment.family_members[0].local_positions, segment.local_positions);
assert.ok(instruction_sources.length > 0);
for (const [site, source] of instruction_sources.entries()) {
  assert.equal(segment.local_positions.origins[source.origin].site, site);
  if (source.kind === 'elided_pure') assert.equal(source.range, null);
  else assert.ok(source.range.begin <= source.range.end);
}
const bytes = Buffer.from(record.source, 'utf8');
const methodBytes = Buffer.from(member.source, 'utf8');
assert.ok(segment.begin > 0 && segment.end < bytes.length);
assert.deepEqual(bytes.subarray(segment.begin, segment.end), methodBytes);
for (const range of [...segment.local_positions.parameters,
                     ...segment.local_positions.expressions,
                     ...segment.local_positions.statements]) {
  assert.ok(range.begin >= 0 && range.end <= methodBytes.length);
  assert.deepEqual(bytes.subarray(segment.begin + range.begin, segment.begin + range.end),
                   methodBytes.subarray(range.begin, range.end));
}
// Selection is explicit, never an option-order-dependent route downgrade.
for (const flags of [
  ['--record-value-slice', '--value-source'],
  ['--value-source', '--record-value-slice'],
  ['--record-value-slice', '--record-source'],
  ['--record-value-slice', '--record-value-slice'],
]) {
  const child = spawnSync(tool, [abc, methodText, ...flags], { encoding: 'utf8' });
  assert.notEqual(child.status, 0);
  assert.equal(child.stdout.includes('"source":'), false);
}
