import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const {diamond, conditionalThrow, layout, effect, scaled} = await import(pathToFileURL(process.argv[2]).href);
assert.equal(scaled(), undefined);
assert.equal(layout(), 42);
for (const input of [false, 0, -0, true, 1, -1, undefined, null, '', '0', 0n, NaN, Symbol(), {}]) {
  const taken = input === false || input === 0;
  assert.equal(diamond(input), taken ? 22 : 11);
  const trace = [];
  const object = {get 0() {trace.push(0); return 100;}, get 1() {trace.push(1); return 200;}};
  assert.equal(effect(object, input), taken ? 200 : 100);
  assert.deepEqual(trace, [taken ? 1 : 0]);
  if (taken) assert.equal(conditionalThrow(input), undefined);
  else {
    let caught = false;
    try {conditionalThrow(input);} catch(error) {caught = true; assert.ok(Object.is(error, input));}
    assert.equal(caught, true);
  }
}
const error = {};
assert.throws(() => effect({get 0() {throw error;}}, true), value => value === error);
