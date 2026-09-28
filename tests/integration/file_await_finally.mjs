import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--await-finally'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!source.includes('INCOMPLETE'));
assert.ok(source.includes('finally'));
const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 9);

for (const [index, capture] of functions.entries()) {
  const saved = [], initial = {};
  let iteration = 0;
  capture(() => iteration++ === 0, () => {
    if (index % 3 === 1) throw initial;
    return initial;
  }, (get, set) => saved.push({get, set}));
  assert.equal(saved.length, 1);
  const {get, set} = saved[0];
  assert.equal(get.length, 2);

  for (const payload of [undefined, null, false, 0, NaN, Symbol('pending'), {}]) {
    const changed = {}, during = {}, cleanupError = {};
    const cases = [() => payload, () => Promise.resolve(payload), () => Promise.reject(payload),
      log => ({get then() { log.push('get-then'); throw payload; }}),
      ...[false, true].map(reject => (log, update) => {
        const input = {get then() {
          log.push('get-then');
          return function (resolve, fail) {
            assert.equal(this, input); log.push('call-then'); update(during);
            (reject ? fail : resolve)(payload);
          };
        }};
        return input;
      })];
    for (const makeInput of cases) for (const cleanupThrows of [false, true]) {
      async function observe(recovered) {
        let current = initial;
        set(current);
        const log = [];
        const update = value => { current = value; set(value); };
        const oracle = async function (input, cleanup) {
          try { await input; } finally { cleanup(current); }
          return current;
        };
        const input = makeInput(log, update);
        const promise = (recovered ? get : oracle)(input, function (cell) {
          assert.equal(this, undefined); assert.equal(cell, current);
          log.push('cleanup'); update(changed);
          if (cleanupThrows) throw cleanupError;
        });
        assert.ok(promise instanceof Promise);
        log.push('returned');
        let result, rejected = false;
        const settled = promise.then(value => { result = value; log.push('fulfilled'); },
          error => { result = error; rejected = true; log.push('rejected'); });
        queueMicrotask(() => log.push('external-microtask'));
        await settled;
        return {result, rejected, log};
      }
      const expected = await observe(false), actual = await observe(true);
      assert.ok(Object.is(actual.result, expected.result));
      assert.equal(actual.rejected, expected.rejected);
      assert.deepEqual(actual.log, expected.log);
      assert.equal(actual.log.filter(event => event === 'cleanup').length, 1);
    }
  }

  // Suspension is not an exit from try: finally cannot run before settlement.
  let resume;
  const log = [], changed = {};
  const result = get(new Promise(resolve => { resume = resolve; }), cell => {
    assert.equal(cell, changed); log.push('cleanup');
  });
  await Promise.resolve();
  assert.deepEqual(log, []);
  set(changed); resume();
  assert.equal(await result, changed);
  assert.deepEqual(log, ['cleanup']);
}
