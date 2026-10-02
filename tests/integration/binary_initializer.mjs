import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';

// Handwritten behavior, independent of the production opcode/token tables.
const operations = [
  (a,b) => a + b, (a,b) => a - b, (a,b) => a * b, (a,b) => a / b,
  (a,b) => a % b, (a,b) => a == b, (a,b) => a != b, (a,b) => a < b,
  (a,b) => a <= b, (a,b) => a > b, (a,b) => a >= b, (a,b) => a << b,
  (a,b) => a >>> b, (a,b) => a >> b, (a,b) => a & b, (a,b) => a | b,
  (a,b) => a ^ b, (a,b) => a ** b, (a,b) => a in b, (a,b) => a instanceof b,
  (a,b) => a !== b, (a,b) => a === b
];
let scenarios = 0;
for (const [index, reference] of operations.entries()) for (const negative of [false, true]) {
  const source = execFileSync(process.argv[2], [negative ? '--negative-source' : '--source', String(index)], {encoding: 'utf8'});
  const generated = runInNewContext(source);
  const left = negative ? -7 : 7;
  const right = index === 18 ? {} : index === 19 ? function Constructor() {} : negative ? 3 : 9;
  const result = generated(right);
  assert.deepEqual(Object.keys(result), ['field0', 'field1']);
  assert.deepEqual(Object.values(result), [reference(left, right), reference(left, right)]);
  ++scenarios;
  if (index === 19) for (const failAt of [0, 1, 2]) {
    const exercise = fn => {
      const events = [], failure = {}; let calls = 0;
      const constructor = {[Symbol.hasInstance](value) {
        events.push(value); if (++calls === failAt) throw failure; return calls === 1;
      }};
      try { events.push(Object.values(fn(constructor))); }
      catch (error) { assert.equal(error, failure); events.push('throw'); }
      return events;
    };
    assert.deepEqual(exercise(generated), exercise(constructor => ({field0: left instanceof constructor, field1: left instanceof constructor})));
    ++scenarios;
  }
}
const exponentSource = execFileSync(process.argv[3], ['--source'], {encoding: 'utf8'});
const samples = runInNewContext(exponentSource.replace('export default', ''));
const view = new DataView(new ArrayBuffer(8));
const number = hex => { view.setBigUint64(0, BigInt(`0x${hex}`)); return view.getFloat64(0); };
for (const sample of samples) {
  assert.ok(Object.is(sample.fn(), number(sample.left) ** number(sample.right)), `${sample.left} ** ${sample.right}`);
  ++scenarios;
}
const nestedSource = execFileSync(process.argv[4], ['--source'], {encoding: 'utf8'});
for (const sample of runInNewContext(nestedSource.replace('export default', ''))) {
  let expected = sample.seed;
  for (let i = 0; i < 2; ++i) expected = expected ** sample.step;
  assert.ok(Object.is(sample.fn(), expected)); ++scenarios;
}
console.log(`Decoded binary/comparison expressions: ${scenarios} native scenarios passed`);
