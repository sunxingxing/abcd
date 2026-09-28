import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const sharedSource = execFileSync(process.argv[2], ['--compiler-finally'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!sharedSource.includes('INCOMPLETE'));
const {functions: sharedFinalizers} = await import(`data:text/javascript;base64,${Buffer.from(sharedSource).toString('base64')}`);
assert.equal(sharedFinalizers.length, 3);
const depthSource = execFileSync(process.argv[2], ['--compiler-finally-depth-source'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!depthSource.includes('INCOMPLETE'));
const {functions: depthFinalizers} = await import(`data:text/javascript;base64,${Buffer.from(depthSource).toString('base64')}`);
assert.equal(depthFinalizers.length, 5);
sharedFinalizers.push(...depthFinalizers);
const sharedLoopSource = execFileSync(process.argv[2], ['--compiler-finally-loop'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!sharedLoopSource.includes('INCOMPLETE'));
const {functions: sharedLoopFinalizers} = await import(`data:text/javascript;base64,${Buffer.from(sharedLoopSource).toString('base64')}`);
assert.equal(sharedLoopFinalizers.length, 3);
for (const fn of sharedLoopFinalizers) for (const iterations of [0, 1, 4])
  for (const throwAt of [-1, 0, iterations]) for (const cleanupThrows of [false, true])
    for (const afterThrows of [false, true]) for (const pending of [undefined, null, NaN, Symbol('loop'), {}]) {
      const log = [], cleanupError = {}, afterError = {}, result = {};
      let calls = 0, returned, thrown, didThrow = false;
      try {
        returned = fn(function () {
          assert.equal(this, undefined);
          const index = calls++;
          log.push(`get:${index}`);
          if (index === throwAt) throw pending;
          assert.ok(index <= iterations, 'loop failed to exit');
          return index < iterations;
        }, function () {
          assert.equal(this, undefined); log.push('cleanup');
          if (cleanupThrows) throw cleanupError;
        }, function () {
          assert.equal(this, undefined); log.push('after');
          if (afterThrows) throw afterError;
          return result;
        });
      } catch (error) { didThrow = true; thrown = error; }
      const getThrows = throwAt >= 0;
      const expected = Array.from({length: getThrows ? throwAt + 1 : iterations + 1}, (_, i) => `get:${i}`);
      expected.push('cleanup');
      if (!getThrows && !cleanupThrows) expected.push('after');
      assert.deepEqual(log, expected);
      assert.equal(didThrow, getThrows || cleanupThrows || afterThrows);
      assert.equal(didThrow ? thrown : returned,
        cleanupThrows ? cleanupError : getThrows ? pending : afterThrows ? afterError : result);
    }
const catchLoopSource = execFileSync(process.argv[2], ['--compiler-finally-catch-loop'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!catchLoopSource.includes('INCOMPLETE'));
const {functions: catchLoopFinalizers} = await import(`data:text/javascript;base64,${Buffer.from(catchLoopSource).toString('base64')}`);
assert.equal(catchLoopFinalizers.length, 3);
const sharedTransferSource = execFileSync(process.argv[2], ['--compiler-finally-nested-transfer'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!sharedTransferSource.includes('INCOMPLETE'));
const {functions: sharedTransfers} = await import(`data:text/javascript;base64,${Buffer.from(sharedTransferSource).toString('base64')}`);
assert.equal(sharedTransfers.length, 3);
for (const fn of sharedTransfers) for (const iterations of [0, 1, 4])
  for (const breakAt of [-1, 0, 2]) for (const failure of ['none', 'condition', 'body'])
    for (const cleanupThrows of [false, true]) for (const pending of [undefined, null, NaN, Symbol('transfer'), {}]) {
      const log = [], cleanupError = {}, result = {};
      let conditionCalls = 0, bodyCalls = 0, returned, thrown, didThrow = false;
      try {
        returned = fn(function () {
          assert.equal(this, undefined);
          const index = bodyCalls++; log.push(`body:${index}`);
          if (failure === 'body') throw pending;
          return index !== breakAt;
        }, function () {
          assert.equal(this, undefined); log.push('cleanup');
          if (cleanupThrows) throw cleanupError;
        }, function () {
          assert.equal(this, undefined); log.push('after'); return result;
        }, function () {
          assert.equal(this, undefined);
          const index = conditionCalls++; log.push(`condition:${index}`);
          if (failure === 'condition') throw pending;
          assert.ok(index <= iterations, 'nested continue skipped loop exit');
          return index < iterations;
        });
      } catch (error) { didThrow = true; thrown = error; }
      const expected = [];
      let bodyThrows = false;
      for (let i = 0; i <= iterations; ++i) {
        expected.push(`condition:${i}`);
        if (failure === 'condition' || i === iterations) break;
        expected.push(`body:${i}`);
        if (failure === 'body') { bodyThrows = true; break; }
        if (i === breakAt) break;
      }
      const protectedThrows = failure === 'condition' || bodyThrows;
      expected.push('cleanup');
      if (!protectedThrows && !cleanupThrows) expected.push('after');
      assert.deepEqual(log, expected);
      assert.equal(didThrow, protectedThrows || cleanupThrows);
      assert.equal(didThrow ? thrown : returned, cleanupThrows ? cleanupError : protectedThrows ? pending : result);
    }
for (const fn of catchLoopFinalizers) for (const iterations of [0, 1, 4])
  for (const getThrows of [false, true]) for (const throwAt of [-1, 0, iterations])
    for (const cleanupThrows of [false, true]) for (const afterThrows of [false, true])
      for (const pending of [undefined, null, false, NaN, Symbol('caught'), {}]) {
        const log = [], catchError = {}, cleanupError = {}, afterError = {}, result = {};
        let calls = 0, returned, thrown, didThrow = false;
        try {
          returned = fn(function () {
            assert.equal(this, undefined); log.push('get');
            if (getThrows) throw pending;
          }, function () {
            assert.equal(this, undefined); log.push('cleanup');
            if (cleanupThrows) throw cleanupError;
          }, function () {
            assert.equal(this, undefined); log.push('after');
            if (afterThrows) throw afterError;
            return result;
          }, function (caught) {
            assert.equal(this, undefined); assert.equal(caught, pending);
            const index = calls++;
            log.push(`catch:${index}`);
            if (index === throwAt) throw catchError;
            assert.ok(index <= iterations, 'catch loop failed to exit');
            return index < iterations;
          });
        } catch (error) { didThrow = true; thrown = error; }
        const catchThrows = getThrows && throwAt >= 0;
        const expected = ['get'];
        if (getThrows) for (let i = 0; i <= (catchThrows ? throwAt : iterations); ++i)
          expected.push(`catch:${i}`);
        expected.push('cleanup');
        if (!catchThrows && !cleanupThrows) expected.push('after');
        assert.deepEqual(log, expected);
        assert.equal(didThrow, catchThrows || cleanupThrows || afterThrows);
        assert.equal(didThrow ? thrown : returned,
          cleanupThrows ? cleanupError : catchThrows ? catchError : afterThrows ? afterError : result);
      }
for (const fn of sharedFinalizers) for (const pending of [undefined, null, false, 0, NaN, Symbol('pending'), {},
  {valueOf() { assert.fail('unexpected coercion'); }, toString() { assert.fail('unexpected coercion'); }}]) {
  for (const getThrows of [false, true]) for (const cleanupThrows of [false, true]) for (const afterThrows of [false, true]) {
    const log = [], result = {}, cleanupError = {}, afterError = {};
    let returned, thrown, didThrow = false;
    try {
      returned = fn(function () { assert.equal(this, undefined); log.push('get'); if (getThrows) throw pending; return pending; },
        function () { assert.equal(this, undefined); log.push('cleanup'); if (cleanupThrows) throw cleanupError; },
        function () { assert.equal(this, undefined); log.push('after'); if (afterThrows) throw afterError; return result; });
    } catch (error) { didThrow = true; thrown = error; }
    assert.deepEqual(log, getThrows || cleanupThrows ? ['get', 'cleanup'] : ['get', 'cleanup', 'after']);
    assert.equal(didThrow, getThrows || cleanupThrows || afterThrows);
    assert.equal(didThrow ? thrown : returned, cleanupThrows ? cleanupError : getThrows ? pending : afterThrows ? afterError : result);
  }
}

const nestedSharedSource = execFileSync(process.argv[2], ['--compiler-finally-nested'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!nestedSharedSource.includes('INCOMPLETE'));
const {functions: nestedShared} = await import(`data:text/javascript;base64,${Buffer.from(nestedSharedSource).toString('base64')}`);
assert.equal(nestedShared.length, 6);
for (const [index, fn] of nestedShared.entries()) {
  const rethrow = index % 2 === 0;
  for (const pending of [undefined, null, NaN, Symbol('pending'), {}])
    for (const getThrows of [false, true]) for (const catchThrows of [false, true])
      for (const cleanupThrows of [false, true]) for (const afterThrows of [false, true]) {
        const log = [], caughtError = {}, cleanupError = {}, afterError = {}, result = {};
        let didThrow = false, returned, thrown;
        try {
          returned = fn(function () { assert.equal(this, undefined); log.push('get'); if (getThrows) throw pending; },
            function () { assert.equal(this, undefined); log.push('cleanup'); if (cleanupThrows) throw cleanupError; },
            function () { assert.equal(this, undefined); log.push('after'); if (afterThrows) throw afterError; return result; },
            function (error) { assert.equal(this, undefined); log.push('catch'); assert.equal(error, pending); if (catchThrows) throw caughtError; });
        } catch (error) { didThrow = true; thrown = error; }
        const pendingThrow = getThrows && (rethrow || catchThrows);
        const expected = ['get'];
        if (getThrows && !rethrow) expected.push('catch');
        expected.push('cleanup');
        if (!pendingThrow && !cleanupThrows) expected.push('after');
        assert.deepEqual(log, expected);
        assert.equal(didThrow, pendingThrow || cleanupThrows || afterThrows);
        assert.equal(didThrow ? thrown : returned,
          cleanupThrows ? cleanupError : pendingThrow ? (rethrow ? pending : caughtError) : afterThrows ? afterError : result);
      }
}
const catchReturnSource = execFileSync(process.argv[2], ['--compiler-finally-catch-return'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!catchReturnSource.includes('INCOMPLETE'));
const {functions: catchReturns} = await import(`data:text/javascript;base64,${Buffer.from(catchReturnSource).toString('base64')}`);
assert.equal(catchReturns.length, 3);
for (const fn of catchReturns) for (const pending of [undefined, Symbol('return'), {}]) {
  const log = [];
  assert.equal(fn(() => { log.push('get'); throw pending; }, () => { log.push('cleanup'); },
    () => { log.push('after'); }), pending);
  assert.deepEqual(log, ['get']);
}

const bypassSource = execFileSync(process.argv[2], ['--compiler-finally-early-return'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!bypassSource.includes('INCOMPLETE'));
const {functions: bypasses} = await import(`data:text/javascript;base64,${Buffer.from(bypassSource).toString('base64')}`);
assert.equal(bypasses.length, 3);
for (const fn of bypasses) for (const bypass of [false, true]) {
  const log = [], result = {};
  const returned = fn(() => { log.push('get'); }, () => { log.push('cleanup'); },
    () => { log.push('after'); return result; }, bypass);
  assert.deepEqual(log, bypass ? [] : ['get', 'cleanup', 'after']);
  assert.equal(returned, bypass ? undefined : result);
}

const sharedChainSource = execFileSync(process.argv[2], ['--compiler-finally-chain'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!sharedChainSource.includes('INCOMPLETE'));
const {functions: sharedChains} = await import(`data:text/javascript;base64,${Buffer.from(sharedChainSource).toString('base64')}`);
assert.equal(sharedChains.length, 6);
for (const [index, fn] of sharedChains.entries()) {
  const count = index % 2 + 2;
  for (const failAt of [0, 1, count]) for (const failure of ['get', 'cleanup', 'after']) {
    const log = [], value = {}, pending = Symbol('pending');
    let iteration = 0, returned, thrown, didThrow = false;
    const call = (name) => {
      if (name === 'get') ++iteration;
      log.push(`${iteration}:${name}`);
      if (iteration === failAt && name === failure) throw pending;
      return value;
    };
    try { returned = fn(() => call('get'), () => call('cleanup'), () => call('after')); }
    catch (error) { didThrow = true; thrown = error; }
    const expected = [];
    for (let i = 1; i <= (failAt || count); ++i) {
      expected.push(`${i}:get`, `${i}:cleanup`);
      if (i !== failAt || failure === 'after') expected.push(`${i}:after`);
    }
    assert.deepEqual(log, expected);
    assert.equal(didThrow, failAt !== 0);
    assert.equal(didThrow ? thrown : returned, failAt ? pending : value);
  }
}

const bareTransferSource = execFileSync(process.argv[2], ['--finally-bare-transfer'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!bareTransferSource.includes('INCOMPLETE'));
const {functions: bareTransfers} = await import(`data:text/javascript;base64,${Buffer.from(bareTransferSource).toString('base64')}`);
assert.equal(bareTransfers.length, 12);
for (const [index, fn] of bareTransfers.entries()) for (const count of [0, 1, 3]) {
  for (const choice of [0, 1]) for (const runThrows of [false, true]) {
    for (const pending of [undefined, null, Symbol('pending'), {}]) for (const afterThrows of [false, true]) {
      const stop = index % 2 === 1, different = index % 4 >= 2, log = [], value = {}, afterError = {};
      let next = 0, returned, thrown, didThrow = false;
      try {
        returned = fn(function () {
          assert.equal(this, undefined); log.push('next');
          assert.ok(next <= count); return next++ < count;
        }, function () {
          assert.equal(this, undefined); log.push('run'); if (runThrows) throw pending;
        }, function () {
          assert.equal(this, undefined); log.push('after'); if (afterThrows) throw afterError;
          return value;
        }, choice);
      } catch (error) { didThrow = true; thrown = error; }
      const expected = [];
      if ((stop !== (different && runThrows)) && choice && count > 0) expected.push('next', 'run');
      else {
        for (let i = 0; i < count; ++i) {
          expected.push('next'); if (choice) expected.push('run');
        }
        expected.push('next');
      }
      expected.push('after');
      assert.deepEqual(log, expected);
      assert.equal(didThrow, afterThrows);
      assert.equal(didThrow ? thrown : returned, afterThrows ? afterError : value);
    }
  }
  const nextError = {};
  assert.throws(() => fn(() => { throw nextError; }, () => assert.fail('run'),
    () => assert.fail('after'), 1), error => error === nextError);
}

const outerTransferSource = execFileSync(process.argv[2], ['--finally-outer-transfer'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!outerTransferSource.includes('INCOMPLETE'));
const {functions: outerTransfers} = await import(`data:text/javascript;base64,${Buffer.from(outerTransferSource).toString('base64')}`);
assert.equal(outerTransfers.length, 6);
for (const [index, fn] of outerTransfers.entries()) for (const outerCount of [0, 1, 3]) {
  for (const innerCount of [0, 1, 2]) for (const choice of [0, 1]) {
    for (const runThrows of [false, true]) for (const cleanupThrows of [false, true]) {
      const stop = index % 2 === 1, log = [], errors = [{}, {}], value = {};
      let outer = 0, inner = 0, returned, thrown, didThrow = false;
      try {
        returned = fn(function () {
          assert.equal(this, undefined); log.push('outer'); inner = 0;
          assert.ok(outer <= outerCount); return outer++ < outerCount;
        }, function () {
          assert.equal(this, undefined); log.push('inner');
          assert.ok(inner <= innerCount); return inner++ < innerCount;
        }, function () { assert.equal(this, undefined); log.push('run'); if (runThrows) throw errors[0]; },
        function () { assert.equal(this, undefined); log.push('cleanup'); if (cleanupThrows) throw errors[1]; },
        function () { assert.equal(this, undefined); log.push('after'); return value; }, choice);
      } catch (error) { didThrow = true; thrown = error; }
      const expected = []; let failed = false;
      outerLoop: for (let o = 0; ; ++o) {
        expected.push('outer'); if (o === outerCount) break;
        for (let i = 0; ; ++i) {
          expected.push('inner'); if (i === innerCount) break;
          if (!choice) continue;
          expected.push('run', 'cleanup');
          if (runThrows || cleanupThrows) { failed = true; break outerLoop; }
          if (stop) break outerLoop;
          continue outerLoop;
        }
      }
      if (!failed) expected.push('after');
      assert.deepEqual(log, expected); assert.equal(didThrow, failed);
      assert.equal(didThrow ? thrown : returned, failed ? errors[cleanupThrows ? 1 : 0] : value);
    }
  }
}

const transferSource = execFileSync(process.argv[2], ['--finally-transfer'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!transferSource.includes('INCOMPLETE'));
const {functions: transfers} = await import(`data:text/javascript;base64,${Buffer.from(transferSource).toString('base64')}`);
assert.equal(transfers.length, 18);
for (const [index, fn] of transfers.entries()) for (const count of [0, 1, 3]) for (const choice of [0, 1]) {
  for (const runThrows of [false, true]) for (const cleanupThrows of [false, true]) {
    const stop = index % 2 === 1, overrides = index % 6 >= 2, different = index % 6 >= 4;
    const log = [], runError = {}, cleanupError = {}, result = {};
    let next = 0, returned, thrown, didThrow = false;
    try {
      returned = fn(function () { assert.equal(this, undefined); log.push('next'); return next++ < count; },
        function () { assert.equal(this, undefined); log.push('run'); if (runThrows) throw runError; },
        function () { assert.equal(this, undefined); log.push('cleanup'); if (cleanupThrows) throw cleanupError; },
        function () { assert.equal(this, undefined); log.push('after'); return result; }, choice);
    } catch (error) { didThrow = true; thrown = error; }
    const expected = [];
    let failed = false;
    for (let iteration = 0; ; ++iteration) {
      expected.push('next');
      if (iteration === count) break;
      if (!choice) continue;
      expected.push('run', 'cleanup');
      if ((!overrides && runThrows) || cleanupThrows) { failed = true; break; }
      if (stop !== (different && runThrows)) break;
    }
    if (!failed) expected.push('after');
    assert.deepEqual(log, expected);
    assert.equal(didThrow, failed);
    assert.equal(didThrow ? thrown : returned, failed ? cleanupThrows ? cleanupError : runError : result);
  }
}

const nestedSource = execFileSync(process.argv[2], ['--nested-finally'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!nestedSource.includes('INCOMPLETE'));
const {functions: nested} = await import(`data:text/javascript;base64,${Buffer.from(nestedSource).toString('base64')}`);
assert.equal(nested.length, 9);
for (const [mode, fn] of nested.entries()) for (const initial of [undefined, null, -0, NaN, Symbol('pending'), {}]) {
  for (const getThrows of [false, true]) for (const innerThrows of [false, true]) {
    for (const outerThrows of [false, true]) for (const afterThrows of [false, true]) {
      const flags = [getThrows, innerThrows, outerThrows, afterThrows];
      const errors = [initial, {}, {}, {}], result = {}, log = [];
      let current = initial;
      let returned, thrown, didThrow = false;
      try {
        returned = fn(...['get', 'inner', 'outer', 'after'].map((name, index) => function () {
          assert.equal(this, undefined); log.push(name);
          if (flags[index]) throw errors[index];
          if (index === 0) return current;
          current = result;
          return result;
        }));
      } catch (error) { didThrow = true; thrown = error; }
      const pending = outerThrows ? 2 : innerThrows ? 1 : getThrows || mode % 3 === 2 ? 0 : mode % 3 === 0 && afterThrows ? 3 : -1;
      assert.deepEqual(log, getThrows || innerThrows || outerThrows || mode % 3 !== 0 ?
        ['get', 'inner', 'outer'] : ['get', 'inner', 'outer', 'after']);
      assert.equal(didThrow, pending !== -1);
      assert.ok(Object.is(didThrow ? thrown : returned,
        pending === -1 ? mode % 3 === 1 ? initial : result : errors[pending]));
    }
  }
}

const fallthroughSource = execFileSync(process.argv[2], ['--finally-fallthrough'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!fallthroughSource.includes('INCOMPLETE'));
const {functions: fallthroughs} = await import(`data:text/javascript;base64,${Buffer.from(fallthroughSource).toString('base64')}`);
assert.equal(fallthroughs.length, 9);
for (const [index, fn] of fallthroughs.entries()) for (const choice of [0, 1]) for (const getThrows of [false, true]) {
  for (const cleanupThrows of [false, true]) for (const afterThrows of [false, true]) {
    const log = [], errors = [{}, {}, {}], value = {};
    const alternative = index % 3 !== 0 && !choice;
    const bypassCleanup = index % 3 === 2 && !choice && !getThrows;
    let returned, thrown, didThrow = false;
    try {
      returned = fn(...['get', 'cleanup', 'after'].map((name, index) => function () {
        assert.equal(this, undefined); log.push(name);
        if ([getThrows, cleanupThrows, afterThrows][index]) throw errors[index];
        return value;
      }), choice, function () {
        assert.equal(this, undefined); log.push('alternative');
        if (afterThrows) throw errors[2];
        return value;
      });
    } catch (error) { didThrow = true; thrown = error; }
    assert.deepEqual(log, bypassCleanup ? ['get', 'alternative'] :
      getThrows || cleanupThrows ? ['get', 'cleanup'] : ['get', 'cleanup', alternative ? 'alternative' : 'after']);
    assert.equal(didThrow, getThrows || (!bypassCleanup && cleanupThrows) || afterThrows);
    assert.equal(didThrow ? thrown : returned,
      !bypassCleanup && cleanupThrows ? errors[1] : getThrows ? errors[0] : afterThrows ? errors[2] : value);
  }
}

// The producer requires a real finalizer AST and fresh operation correspondence.
// Execute only complete generated source, never hand-written recovery output.
const source = execFileSync(process.argv[2], ['--finally-return'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!source.includes('INCOMPLETE'));
const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 3);
for (const fn of functions) {
  for (const pending of [undefined, null, -0, NaN, 3n, Symbol('pending'), {}]) {
    for (const getThrows of [false, true]) for (const cleanupThrows of [false, true]) {
      const log = [], replacement = {}, cleanupError = {};
      let current = pending, result;
      try {
        result = fn(function () {
          assert.equal(this, undefined); log.push('get');
          if (getThrows) throw current;
          return current;
        }, function () {
          assert.equal(this, undefined); log.push('cleanup'); current = replacement;
          if (cleanupThrows) throw cleanupError;
        });
        assert.ok(!getThrows && !cleanupThrows);
        assert.ok(Object.is(result, pending));
      } catch (error) {
        assert.ok(getThrows || cleanupThrows);
        assert.ok(Object.is(error, cleanupThrows ? cleanupError : pending));
      }
      assert.deepEqual(log, ['get', 'cleanup']);
      assert.equal(current, replacement);
    }
  }
}

const pairedSource = execFileSync(process.argv[2], ['--completion-pairs'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!pairedSource.includes('INCOMPLETE'));
const {functions: paired} = await import(`data:text/javascript;base64,${Buffer.from(pairedSource).toString('base64')}`);
assert.equal(paired.length, 15);
for (const [index, fn] of paired.entries()) {
  const mode = index % 5; // chain, different callee, late payload, extra call, wrong rethrow
  for (const getThrows of [false, true]) for (const cleanupFailure of [0, 1, 2]) {
    const pending = {}, cleaned = {}, failure = {}, log = [];
    let cleanups = 0, returned, thrown, didThrow = false;
    function get() {
      assert.equal(this, undefined); log.push('get');
      if (getThrows) throw pending;
      return pending;
    }
    function cleanup(name) {
      log.push(name);
      if (++cleanups === cleanupFailure) throw failure;
      return cleaned;
    }
    const first = function () { assert.equal(this, undefined); return cleanup('first'); };
    const second = function (value) {
      assert.equal(this, undefined);
      if (mode === 0) assert.equal(value, cleaned);
      return cleanup('second');
    };
    try { returned = fn(get, first, second); }
    catch (error) { didThrow = true; thrown = error; }
    const expected = ['get'];
    const names = mode === 0 ? ['first', 'second'] :
      mode === 1 && getThrows ? ['second'] :
      mode === 3 && getThrows ? ['first', 'first'] : ['first'];
    let overridden = false;
    for (const [step, name] of names.entries()) {
      expected.push(name);
      if (step + 1 === cleanupFailure) { overridden = true; break; }
    }
    assert.deepEqual(log, expected);
    assert.equal(didThrow, getThrows || overridden);
    if (didThrow) assert.equal(thrown, overridden ? failure : mode === 4 ? get : pending);
    else assert.equal(returned, mode === 2 ? cleaned : pending);
  }
}

const branchSource = execFileSync(process.argv[2], ['--finally-branch'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!branchSource.includes('INCOMPLETE'));
const {functions: branches} = await import(`data:text/javascript;base64,${Buffer.from(branchSource).toString('base64')}`);
assert.equal(branches.length, 9);
for (const [index, fn] of branches.entries()) for (const choice of [0, 1]) for (const secondChoice of [0, 1]) {
  for (const getThrows of [false, true]) for (const insideThrows of [false, true]) {
    for (const cleanupThrows of [false, true]) for (const afterThrows of [false, true]) {
      const log = [], value = {}, insideValue = {}, getError = {}, insideError = {}, afterError = {}, cleanupError = {};
      let returned, thrown, didThrow = false;
      try {
        returned = fn(function () {
          assert.equal(this, undefined); log.push('get');
          if (getThrows) throw getError;
          return value;
        }, function () {
          assert.equal(this, undefined); log.push('cleanup');
          if (cleanupThrows) throw cleanupError;
        }, choice, function () {
          assert.equal(this, undefined); log.push('inside');
          if (insideThrows) throw insideError;
          return insideValue;
        }, function () {
          assert.equal(this, undefined); log.push('after');
          if (afterThrows) throw afterError;
        }, secondChoice);
      } catch (error) { didThrow = true; thrown = error; }
      const ranInside = !getThrows && choice === 1 && (index % 3 !== 1 || secondChoice === 1);
      const earlyReturn = index % 3 === 2 && ranInside && !insideThrows;
      const ranAfter = !getThrows && !(ranInside && insideThrows) && !earlyReturn;
      const expected = ['get'];
      if (ranInside) expected.push('inside');
      if (ranAfter) expected.push('after');
      if (!earlyReturn) expected.push('cleanup');
      assert.deepEqual(log, expected);
      assert.equal(didThrow, (!earlyReturn && cleanupThrows) || getThrows || (ranInside && insideThrows) || (ranAfter && afterThrows));
      if (didThrow) assert.equal(thrown, !earlyReturn && cleanupThrows ? cleanupError : getThrows ? getError :
          ranInside && insideThrows ? insideError : afterError);
      else assert.equal(returned, earlyReturn ? insideValue : value);
    }
  }
}

const loopSource = execFileSync(process.argv[2], ['--finally-loop'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!loopSource.includes('INCOMPLETE'));
const {functions: loops} = await import(`data:text/javascript;base64,${Buffer.from(loopSource).toString('base64')}`);
assert.equal(loops.length, 6);
for (const [index, fn] of loops.entries()) for (const iterations of [0, 1, 3]) {
  for (const getThrows of [false, true]) for (const nextFailure of [0, 1, 3]) {
    for (const stepFailure of [0, 1, 2]) for (const cleanupThrows of [false, true]) {
      const log = [], value = {}, getError = {}, nextError = {}, stepError = {}, cleanupError = {};
      const states = [value, {}, {}, {}];
      let nextCalls = 0, stepCalls = 0, returned, thrown, didThrow = false;
      try {
        returned = fn(function () {
          assert.equal(this, undefined); log.push('get');
          if (getThrows) throw getError;
          return value;
        }, function () {
          assert.equal(this, undefined); log.push('cleanup');
          if (cleanupThrows) throw cleanupError;
        }, function () {
          assert.equal(this, undefined); log.push(`next:${++nextCalls}`);
          assert.ok(nextCalls <= iterations + 1);
          if (nextCalls === nextFailure) throw nextError;
          return nextCalls <= iterations;
        }, function (previous) {
          assert.equal(this, undefined); log.push(`step:${++stepCalls}`);
          if (index % 2 === 1) assert.equal(previous, states[stepCalls - 1]);
          if (stepCalls === stepFailure) throw stepError;
          return states[stepCalls];
        });
      } catch (error) { didThrow = true; thrown = error; }
      const expected = ['get'];
      let failure = getThrows ? getError : null;
      if (!failure) for (let i = 1; i <= iterations + 1; ++i) {
        expected.push(`next:${i}`);
        if (i === nextFailure) { failure = nextError; break; }
        if (i > iterations) break;
        expected.push(`step:${i}`);
        if (i === stepFailure) { failure = stepError; break; }
      }
      expected.push('cleanup');
      if (cleanupThrows) failure = cleanupError;
      assert.deepEqual(log, expected);
      assert.equal(didThrow, failure !== null);
      if (didThrow) assert.equal(thrown, failure);
      else assert.equal(returned, index % 2 === 1 ? states[iterations] : value);
    }
  }
}

const catchSource = execFileSync(process.argv[2], ['--finally-catch'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!catchSource.includes('INCOMPLETE'));
const {functions: catches} = await import(`data:text/javascript;base64,${Buffer.from(catchSource).toString('base64')}`);
assert.equal(catches.length, 6);
for (const [index, fn] of catches.entries()) for (const insideError of [undefined, {}, Symbol('inside')]) {
  for (const getThrows of [false, true]) for (const insideThrows of [false, true]) {
    for (const handleThrows of [false, true]) for (const afterThrows of [false, true]) {
      for (const cleanupThrows of [false, true]) {
        const log = [], value = {}, handled = {}, getError = {}, handleError = {}, afterError = {}, cleanupError = {};
        let returned, thrown, didThrow = false;
        try {
          returned = fn(function () {
            assert.equal(this, undefined); log.push('get');
            if (getThrows) throw getError;
            return value;
          }, function () {
            assert.equal(this, undefined); log.push('cleanup');
            if (cleanupThrows) throw cleanupError;
          }, function () {
            assert.equal(this, undefined); log.push('inside');
            if (insideThrows) throw insideError;
          }, function (error) {
            assert.equal(this, undefined); assert.equal(error, insideError); log.push('handle');
            if (handleThrows) throw handleError;
            return handled;
          }, function () {
            assert.equal(this, undefined); log.push('after');
            if (afterThrows) throw afterError;
          });
        } catch (error) { didThrow = true; thrown = error; }
        const expected = ['get'];
        const early = index % 2 === 1 && !getThrows && insideThrows && !handleThrows;
        let failure = getThrows ? getError : null;
        if (!getThrows) {
          expected.push('inside');
          if (insideThrows) { expected.push('handle'); if (handleThrows) failure = handleError; }
          if (!failure && !early) { expected.push('after'); if (afterThrows) failure = afterError; }
        }
        if (!early) { expected.push('cleanup'); if (cleanupThrows) failure = cleanupError; }
        assert.deepEqual(log, expected);
        assert.equal(didThrow, failure !== null);
        if (didThrow) assert.equal(thrown, failure);
        else assert.equal(returned, early ? handled : value);
      }
    }
  }
}

const overrideSource = execFileSync(process.argv[2], ['--finally-override'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!overrideSource.includes('INCOMPLETE'));
const {functions: overrides} = await import(`data:text/javascript;base64,${Buffer.from(overrideSource).toString('base64')}`);
assert.equal(overrides.length, 24);
for (const [index, fn] of overrides.entries()) {
  for (const result of [undefined, null, -0, NaN, 3n, Symbol('replacement'), {}]) {
    for (const getThrows of [false, true]) for (const cleanupThrows of [false, true]) {
      const log = [], pending = {}, failure = {};
      const argument = index % 4 === 3;
      const cleanup = function () {
        assert.equal(this, undefined); log.push('cleanup');
        if (cleanupThrows) throw failure;
        return result;
      };
      let returned, thrown, didThrow = false;
      try {
        returned = fn(function () {
          assert.equal(this, undefined); log.push('get');
          if (getThrows) throw pending;
          return pending;
        }, argument ? result : cleanup);
      } catch (error) { didThrow = true; thrown = error; }
      const raises = index % 8 >= 4;
      const hasCleanup = index % 4 < 2;
      const keepsPayload = index % 4 === 1 && getThrows;
      const expected = argument ? result : !hasCleanup ? undefined : cleanupThrows ? failure : keepsPayload ? pending : result;
      assert.deepEqual(log, hasCleanup ? ['get', 'cleanup'] : ['get']);
      assert.equal(didThrow, (hasCleanup && cleanupThrows) || raises);
      assert.ok(Object.is(didThrow ? thrown : returned, expected));
    }
  }
}
