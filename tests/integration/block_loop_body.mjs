import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const {zero, nonzero, spin, sequential, chosen} = await import(pathToFileURL(process.argv[2]).href);

function run(fn, values) {
  const trace = [];
  let calls = 0;
  const step = new Proxy(function () {}, {
    apply(target, receiver, args) {
      assert.equal(receiver, undefined);
      assert.deepEqual(args, []);
      assert.ok(calls < values.length, 'extra loop iteration');
      trace.push(calls);
      return values[calls++];
    },
    get() { throw new Error('callee property lookup'); },
  });
  const result = fn(step);
  assert.ok(Object.is(result, values.at(-1)));
  assert.deepEqual(trace, values.map((_, i) => i));
}

for (const exit of [true, 1, -1, NaN, undefined, null, '', '0', 0n, Symbol(), {}]) {
  run(zero, [exit]);
  run(zero, [false, 0, -0, exit]);
}
for (const exit of [false, 0, -0, undefined, null, '', '0', 0n, Symbol(), {}]) {
  run(nonzero, [exit]);
  run(nonzero, [true, 1, -1, NaN, exit]);
}
run(sequential, [false, 0, true, 1, true, 0]);
run(step => chosen(step, true), [false, 0, 42]);
for (const skip of [false, 0, -0]) {
  assert.ok(Object.is(chosen(() => {throw new Error('bypass called loop');}, skip), skip));
}
for (const fn of [zero, nonzero, spin]) {
  const error = {};
  let calls = 0;
  const again = fn === nonzero ? true : false;
  assert.throws(() => fn(() => {
    if (++calls === 4) throw error;
    return again;
  }), caught => caught === error);
  assert.equal(calls, 4);
}
for (const fn of [zero, nonzero, spin]) {
  assert.throws(() => fn({}), TypeError);
}
