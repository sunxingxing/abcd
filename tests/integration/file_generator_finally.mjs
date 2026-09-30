import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

for (const mode of ['return', 'override-return', 'override-throw']) {
const source = execFileSync(process.argv[2], [`--yield-finally-${mode}`], {encoding: 'utf8', timeout: 60_000});
assert.ok(!source.includes('INCOMPLETE')); assert.ok(source.includes('finally'));
const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 9);
for (const [index, capture] of functions.entries()) {
  const saved = []; let iteration = 0;
  capture(() => iteration++ === 0, () => {
    if (index % 3 === 1) throw 'cell';
    return 'cell';
  }, get => saved.push(get));
  assert.equal(saved.length, 1);
  const get = saved[0]; assert.equal(get.length, 2);
  for (const pending of [undefined, null, false, 0, NaN, Symbol('pending'), {}]) {
    for (const failedWork of [false, true]) for (const failedCleanup of [false, true]) {
      for (const request of ['next', 'return', 'throw']) {
        function observe(recovered) {
          const log = [], cleanupError = {}, injected = {}, yielded = {get then() { assert.fail('sync yield assimilated thenable'); }};
          const work = function () { assert.equal(this, undefined); log.push('work'); if (failedWork) throw pending; return pending; };
          const cleanup = function () { assert.equal(this, undefined); log.push('cleanup'); if (failedCleanup) throw cleanupError; return yielded; };
          const oracle = mode === 'override-return' ? function* (work, cleanup) {
            try { work(); } finally { return yield cleanup(); }
          } : mode === 'override-throw' ? function* (work, cleanup) {
            try { work(); } finally { throw yield cleanup(); }
          } : function* (work, cleanup) { try { return work(); } finally { yield cleanup(); } };
          const iterator = (recovered ? get : oracle)(work, cleanup);
          assert.deepEqual(log, []);
          let first;
          try { first = iterator.next('ignored'); } catch (error) { assert.equal(error, cleanupError); }
          assert.deepEqual(log, ['work', 'cleanup']);
          if (failedCleanup) {
            assert.equal(first, undefined); assert.deepEqual(iterator.next(), {value: undefined, done: true}); return;
          }
          assert.deepEqual(first, {value: yielded, done: false});
          let result, thrown = false;
          try { result = iterator[request](injected); }
          catch (error) {
            thrown = true;
            assert.ok(Object.is(error, request === 'next' && mode === 'return' ? pending : injected));
          }
          assert.equal(thrown, request === 'throw' || (request === 'next' && (mode === 'override-throw' || (mode === 'return' && failedWork))));
          if (!thrown) {
            assert.equal(result.done, true);
            assert.ok(Object.is(result.value, request === 'return' || mode === 'override-return' ? injected : pending));
          }
          assert.deepEqual(log, ['work', 'cleanup']);
          assert.deepEqual(iterator.next(), {value: undefined, done: true});
        }
        observe(false); observe(true);
      }
    }
  }
  for (const kind of ['return', 'throw']) {
    const payload = {}, iterator = get(() => assert.fail('early completion ran work'), () => assert.fail('early completion ran cleanup'));
    if (kind === 'return') assert.deepEqual(iterator.return(payload), {value: payload, done: true});
    else assert.throws(() => iterator.throw(payload), error => error === payload);
    assert.deepEqual(iterator.next(), {value: undefined, done: true});
  }
  const aValue = {}, bError = {}, aCleanup = {}, bCleanup = {}, log = [];
  const a = get(() => aValue, () => { log.push('a'); return aCleanup; });
  const b = get(() => { throw bError; }, () => { log.push('b'); return bCleanup; });
  assert.deepEqual(a.next(), {value: aCleanup, done: false});
  assert.deepEqual(b.next(), {value: bCleanup, done: false});
  if (mode === 'return') {
    assert.throws(() => b.next(), error => error === bError);
    assert.deepEqual(a.next(), {value: aValue, done: true});
  } else if (mode === 'override-return') {
    assert.deepEqual(b.next(bError), {value: bError, done: true});
    assert.deepEqual(a.next(aValue), {value: aValue, done: true});
  } else {
    assert.throws(() => b.next(bError), error => error === bError);
    assert.throws(() => a.next(aValue), error => error === aValue);
  }
  assert.deepEqual(log, ['a', 'b']);
}
}

// Mismatched cleanup inputs must not be declared equivalent just because both
// contain a yield. Their complete decoded source remains useful and executable.
for (const mode of ['different-callee', 'different-input']) {
  const source = execFileSync(process.argv[2], [`--yield-finally-${mode}`], {encoding: 'utf8', timeout: 60_000});
  assert.ok(!source.includes('INCOMPLETE')); assert.ok(!source.includes('finally'));
  const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
  assert.equal(functions.length, 9);
  for (const [index, capture] of functions.entries()) {
    let iteration = 0; const saved = [];
    capture(() => iteration++ === 0, () => { if (index % 3 === 1) throw 'cell'; return 'cell'; }, get => saved.push(get));
    assert.equal(saved.length, 1);
    const get = saved[0], pending = {}, yielded = {};
    for (const failed of [false, true]) {
      let calls = 0, cleanups = 0;
      const iterator = get(() => { if (++calls === 1 && failed) throw pending; return calls === 1 ? pending : yielded; },
        () => { ++cleanups; return yielded; });
      const first = iterator.next();
      assert.deepEqual(first, {value: failed && mode === 'different-input' ? pending : yielded, done: false});
      if (failed) assert.throws(() => iterator.next(), error => error === pending);
      else assert.deepEqual(iterator.next(), {value: pending, done: true});
      assert.equal(calls, failed && mode === 'different-callee' ? 2 : 1);
      assert.equal(cleanups, failed && mode === 'different-callee' ? 0 : 1);
    }
  }
}

// This raw protocol has no cleanup on injected RETURN. Recovering a finally
// around its protected yield would wrongly add observable cleanup calls.
for (const mode of ['body', 'shared-body']) {
  const source = execFileSync(process.argv[2], [`--yield-finally-${mode}`], {encoding: 'utf8', timeout: 60_000});
  assert.ok(!source.includes('INCOMPLETE'));
  const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
  assert.equal(functions.length, 9);
  for (const [index, capture] of functions.entries()) {
    let iteration = 0; const saved = [];
    capture(() => iteration++ === 0, () => { if (index % 3 === 1) throw 'cell'; return 'cell'; }, get => saved.push(get));
    const get = saved[0]; assert.equal(saved.length, 1);
    for (const request of ['next', 'return', 'throw']) for (const cleanupThrows of [false, true]) {
      const pending = {}, resumed = {}, cleanupError = {}; let workCalls = 0, cleanups = 0;
      const iterator = get(() => { ++workCalls; return pending; }, () => { ++cleanups; if (cleanupThrows) throw cleanupError; });
      assert.deepEqual(iterator.next(), {value: pending, done: false});
      assert.equal(workCalls, 1); assert.equal(cleanups, 0);
      if (request === 'return') assert.deepEqual(iterator.return(resumed), {value: resumed, done: true});
      else if (cleanupThrows || request === 'throw') assert.throws(() => iterator[request](resumed),
        value => value === (cleanupThrows ? cleanupError : resumed));
      else assert.deepEqual(iterator.next(resumed), {value: mode === 'body' ? resumed : 'cell', done: true});
      assert.equal(cleanups, request === 'return' ? 0 : 1);
      assert.deepEqual(iterator.next(), {value: undefined, done: true});
    }
  }
}
