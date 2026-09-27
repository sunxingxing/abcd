import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

// File-backed decoded method, immutable preparation and shared printer.
const result = JSON.parse(execFileSync(process.argv[2], ['--literal-json'], { encoding: 'utf8' }));
const Recovered = Function(`"use strict"; return class { ${result.source} };`)();
const run = Recovered.prototype.run;
const values = [undefined, null, false, -0, NaN, 3n, Symbol('field'), {}, new Proxy({}, {})];
const original = Object.getOwnPropertyDescriptor(Object.prototype, 'run');
try {
  // Own definitions must neither invoke inherited setters nor become ordinary
  // assignments. Reading the input's own getter still occurs exactly once.
  Object.defineProperty(Object.prototype, 'run', {
    configurable: true, set() { assert.fail('inherited setter must not execute'); },
  });
  for (const value of values) {
    let reads = 0;
    const input = { get run() { ++reads; return value; } };
    const object = Reflect.apply(run, null, [input]);
    assert.equal(reads, 1);
    assert.equal(Object.getPrototypeOf(object), Object.prototype);
    assert.deepEqual(Reflect.ownKeys(object), ['run']);
    assert.deepEqual(Object.getOwnPropertyDescriptor(object, 'run'), {
      value, writable: true, enumerable: true, configurable: true,
    });
    assert(Object.is(object.run, value));
    const thrown = { get run() { ++reads; throw value; } };
    assert(Object.is(Reflect.apply(run, null, [thrown]), value));
    assert.equal(reads, 2);
  }
} finally {
  if (original) Object.defineProperty(Object.prototype, 'run', original);
  else delete Object.prototype.run;
}
const pcs = [0, 2, 6, 8, 9, 11, 13, 18, 20, 21];
const map = result.positions;
const bytes = Buffer.from(result.source);
assert.equal(map.operations.length, pcs.length);
assert.equal(map.properties.length, 1);
for (const placement of map.operations) {
  const span = placement.kind === 'anchor' ? map.anchors[placement.node] :
    placement.kind === 'property' ? map.properties[placement.node] :
    placement.kind === 'statement' ? map.statements[placement.node] : map.expressions[placement.node];
  assert.equal(map.origins[span.origin].pc, pcs[placement.operation]);
  if (placement.kind === 'anchor') assert(span.byte <= bytes.length);
  else assert(span.begin < span.end && span.end <= bytes.length);
  if (placement.operation === 6) assert.equal(placement.kind, 'property');
}
