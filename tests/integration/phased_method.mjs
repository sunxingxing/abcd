import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

// Encoded Code header, try table, creation context and name pool are read from
// the fixture file. No hand-built source tree or draft printer participates.
const result = JSON.parse(execFileSync(process.argv[2], ['--exception-json'], { encoding: 'utf8' }));
const Recovered = Function(`"use strict"; return class { ${result.source} };`)();
const instance = new Recovered();
assert.equal(instance.run.length, 1);
const arbitrary = [undefined, null, false, -0, NaN, 19n, Symbol('payload'), {}, new Proxy({}, {})];
for (const value of arbitrary) {
  for (const failure of ['none', 'getter', 'call']) {
    const events = [];
    const input = { get run() {
      events.push('get');
      if (failure === 'getter') throw value;
      return function () {
        assert.equal(this, undefined);
        events.push('call');
        if (failure === 'call') throw value;
        return value;
      };
    } };
    const actual = instance.run(input);
    const expected = failure === 'none' ? !(value === false || value === 0) : value;
    assert(Object.is(actual, expected));
    assert.deepEqual(events, failure === 'getter' ? ['get'] : ['get', 'call']);
  }
}
const map = result.positions;
assert.equal(map.unit, 'utf8_byte');
const bytes = Buffer.from(result.source, 'utf8');
const expectedPcs = [0, 2, 6, 8, 10, 11, 12, 13, 14];
assert.equal(map.operations.length, expectedPcs.length);
const pcs = (origin, mapping = map) => {
  const node = mapping.origins[origin];
  if (node.kind === 'original') return [node.pc];
  assert.equal(node.kind, 'combined');
  return mapping.origin_parts.slice(node.first_part, node.first_part + node.part_count)
    .flatMap(part => pcs(part, mapping));
};
for (const placement of map.operations) {
  const range = placement.kind === 'statement' ? map.statements[placement.node] : map.expressions[placement.node];
  assert(range.begin < range.end && range.end <= bytes.length);
  assert(pcs(range.origin).includes(expectedPcs[placement.operation]));
  if (placement.kind === 'statement')
    assert.match(bytes.subarray(range.begin, range.end).toString('utf8'), /^(if|return)\b/);
}

const forwarded = JSON.parse(execFileSync(process.argv[2], ['--forwarder-json'], { encoding: 'utf8' }));
const Forwarded = Function(`"use strict"; return class { ${forwarded.source} };`)();
const forward = new Forwarded();
for (const value of arbitrary) {
  for (const throws of [false, true]) {
    let reads = 0;
    const input = { get run() { ++reads; if (throws) throw value; return value; } };
    assert(Object.is(forward.run(input), throws ? value : !(value === false || value === 0)));
    assert.equal(reads, 1);
  }
}
const forwardedPcs = [0, 2, 6, 8, 9, 11, 12, 14, 16, 18];
const forwardMap = forwarded.positions;
assert.equal(forwardMap.operations.length, forwardedPcs.length);
for (const placement of forwardMap.operations) {
  const range = placement.kind === 'anchor' ? forwardMap.anchors[placement.node] :
    placement.kind === 'statement' ? forwardMap.statements[placement.node] : forwardMap.expressions[placement.node];
  assert(pcs(range.origin, forwardMap).includes(forwardedPcs[placement.operation]));
  if (placement.kind === 'anchor') assert(range.byte <= Buffer.byteLength(forwarded.source));
  else assert(range.begin < range.end && range.end <= Buffer.byteLength(forwarded.source));
}
