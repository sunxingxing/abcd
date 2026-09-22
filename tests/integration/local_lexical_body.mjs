import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const source = await import(pathToFileURL(process.argv[2]));
const left = Object.freeze({side: 'left'});
const right = Object.freeze({side: 'right'});
for (const value of [undefined, null, false, true, 0, -0, NaN, Infinity,
                     42n, Symbol('cell'), 'value', left]) {
  for (const name of ['roundtrip', 'parent', 'afterPop', 'deprecated', 'wide',
                      'diagnostic', 'omitted']) {
    assert.ok(Object.is(source[name](value, right), value), name);
  }
  assert.ok(Object.is(source.child(left, value), value));
  assert.throws(() => source.thrown(value), error => Object.is(error, value));
}
const events = [];
const callee = new Proxy(function () {}, {
  apply(target, receiver, args) {
    events.push([receiver, args]);
    return Symbol('discarded');
  },
  get() { throw new Error('unexpected property read'); }
});
assert.equal(source.effect(callee, right), right);
assert.deepEqual(events, [[undefined, []]]);
const thrown = Object.freeze({thrown: true});
assert.throws(() => source.effect(() => { throw thrown; }, left),
              error => error === thrown);
assert.throws(() => source.effect(null, right), TypeError);
