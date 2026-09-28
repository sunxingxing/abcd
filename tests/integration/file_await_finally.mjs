import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const suspending = process.argv[3] === '--suspending';
const source = execFileSync(process.argv[2], [suspending ? '--await-finally-suspend' : '--await-finally'],
  {encoding: 'utf8', timeout: 60_000});
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
    const cleanups = suspending ? ['value', 'fulfilled', 'rejected', 'getter-throw', 'resolve-thenable', 'reject-thenable'] : ['value'];
    for (const makeInput of cases) for (const cleanupThrows of [false, true]) for (const cleanup of cleanups) {
      async function observe(recovered) {
        let current = initial;
        set(current);
        const log = [];
        const update = value => { current = value; set(value); };
        const oracle = async function (input, cleanup) {
          try { await input; } finally {
            if (suspending) await cleanup(current);
            else cleanup(current);
          }
          return current;
        };
        const input = makeInput(log, update);
        const promise = (recovered ? get : oracle)(input, function (cell) {
          assert.equal(this, undefined); assert.equal(cell, current);
          log.push('cleanup'); update(changed);
          if (cleanupThrows) throw cleanupError;
          if (cleanup === 'fulfilled') return Promise.resolve(payload);
          if (cleanup === 'rejected') return Promise.reject(cleanupError);
          if (cleanup === 'getter-throw') return {get then() { log.push('cleanup-get-then'); throw cleanupError; }};
          if (cleanup.endsWith('thenable')) {
            const result = {get then() {
              log.push('cleanup-get-then');
              return function (resolve, reject) {
                assert.equal(this, result); log.push('cleanup-call-then'); update(during);
                if (cleanup === 'reject-thenable') reject(cleanupError);
                else resolve(payload);
              };
            }};
            return result;
          }
          return payload;
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

  if (suspending) for (const rejectedInput of [false, true]) for (const rejectedCleanup of [false, true]) {
    // An unresolved cleanup keeps the whole function pending. The original
    // completion and live lexical cell must survive this second suspension.
    const pendingError = {}, replacement = {}, after = {};
    let enterCleanup, resolveCleanup, rejectCleanup, settled = false;
    const entered = new Promise(resolve => { enterCleanup = resolve; });
    const cleanup = new Promise((resolve, reject) => { resolveCleanup = resolve; rejectCleanup = reject; });
    const running = get(rejectedInput ? Promise.reject(pendingError) : undefined, () => {
      enterCleanup(); return cleanup;
    });
    const observed = running.then(value => { settled = true; return {value, rejected: false}; },
      value => { settled = true; return {value, rejected: true}; });
    await entered;
    await Promise.resolve();
    assert.equal(settled, false);
    set(after);
    if (rejectedCleanup) rejectCleanup(replacement);
    else resolveCleanup();
    const outcome = await observed;
    assert.equal(outcome.rejected, rejectedInput || rejectedCleanup);
    assert.equal(outcome.value, rejectedCleanup ? replacement : rejectedInput ? pendingError : after);
  }
}
