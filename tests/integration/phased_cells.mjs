import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

// Actual ABC decoding, preparation, shared tree and printer; no source input.
const result = JSON.parse(execFileSync(process.argv[2], ['--lexical-json'], { encoding: 'utf8' }));
const Recovered = Function(`"use strict"; return class { ${result.source} };`)();
const run = Recovered.prototype.run;
function reference(fn) {
  let cell = fn;
  try { fn(); }
  catch (error) { cell = error; return cell; }
  return cell;
}
const values = [undefined, null, false, -0, NaN, 42, 3n, Symbol('throw'), {}, new Proxy({}, {})];
for (const value of values) {
  for (const throwing of [false, true]) {
    const effects = [];
    const fn = new Proxy(function () {}, {
      apply(target, receiver, args) {
        effects.push([receiver, args]);
        if (throwing) throw value;
        return value;
      },
    });
    const expected = reference(fn);
    const expectedEffects = effects.splice(0);
    const actual = Reflect.apply(run, null, [fn]);
    assert(Object.is(actual, expected));
    assert.deepEqual(effects, expectedEffects);
    assert.deepEqual(effects, [[undefined, []]]);
  }
}
const effects = [];
const thrown = {};
function outer() {
  effects.push('outer');
  const inner = () => { effects.push('inner'); throw thrown; };
  assert.equal(Reflect.apply(run, null, [inner]), thrown);
  effects.push('returned');
}
assert.equal(Reflect.apply(run, null, [outer]), outer);
assert.deepEqual(effects, ['outer', 'inner', 'returned']);

const pcs = [0, 2, 4, 6, 8, 10, 11, 12, 14, 16];
const map = result.positions;
const bytes = Buffer.from(result.source);
assert.equal(map.operations.length, pcs.length);
for (const placement of map.operations) {
  const span = placement.kind === 'anchor' ? map.anchors[placement.node] :
    placement.kind === 'statement' ? map.statements[placement.node] : map.expressions[placement.node];
  assert.equal(map.origins[span.origin].pc, pcs[placement.operation]);
  if (placement.kind === 'anchor') assert(span.byte <= bytes.length);
  else assert(span.begin < span.end && span.end <= bytes.length);
}
