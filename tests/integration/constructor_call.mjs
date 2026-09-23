import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {construct0, construct1, construct2, construct3} =
  await import(pathToFileURL(process.argv[2]).href);
for (const [fn, args] of [
  [construct0, []], [construct1, [17, 23]],
  [construct2, [17]], [construct3, [17, 23]],
]) {
  let calls = 0;
  const ctor = new Proxy(function () {}, {
    construct(target, actual, newTarget) {
      ++calls;
      assert.equal(newTarget, ctor);
      assert.deepEqual(actual, args);
      return {actual};
    },
  });
  assert.deepEqual(fn(ctor, 17, 23).actual, args);
  assert.equal(calls, 1);
  assert.throws(() => fn({}), TypeError);
  const thrown = {};
  const failure = new Proxy(function () {}, {
    construct() { throw thrown; },
  });
  assert.throws(() => fn(failure, 17, 23), error => error === thrown);
}
