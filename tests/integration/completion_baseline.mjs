import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

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
assert.equal(transfers.length, 6);
for (const [index, fn] of transfers.entries()) for (const count of [0, 1, 3]) for (const choice of [0, 1]) {
  for (const runThrows of [false, true]) for (const cleanupThrows of [false, true]) {
    const stop = index % 2 === 1, log = [], runError = {}, cleanupError = {}, result = {};
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
      if (runThrows || cleanupThrows) { failed = true; break; }
      if (stop) break;
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
