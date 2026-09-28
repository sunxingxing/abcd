import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

const source = execFileSync(process.argv[2], ['--outer-transfer'], { encoding: 'utf8', timeout: 60000 });
assert.ok(!source.includes('INCOMPLETE'));
const { functions } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 6);
const portsSource = execFileSync(process.argv[2], ['--outer-ports'], { encoding: 'utf8', timeout: 60000 });
assert.ok(!portsSource.includes('INCOMPLETE'));
const { functions: ports } = await import(`data:text/javascript;base64,${Buffer.from(portsSource).toString('base64')}`);
assert.equal(ports.length, 3);
for (const fn of ports) for (const stop of [false, true]) functions.push((...args) => fn(...args, stop));

function reference(stop, nextOuter, nextInner, run, body, after) {
  outer: while (nextOuter()) {
    while (nextInner()) {
      try { run(); }
      catch (_) {
        if (stop) break outer;
        continue outer;
      }
      body();
    }
    after();
  }
}

// Wrong outer target, execution after an abrupt catch, or accidentally
// protecting body/after must change the trace or the escaped object identity.
function execute(fn, rounds, innerRounds, fails, escapeAt) {
  const log = [], caught = {}, escaped = {};
  let outer = -1, inner = -1;
  function event(name) {
    assert.equal(this, undefined);
    log.push([name, outer, inner]);
    if (name === escapeAt) throw escaped;
  }
  let result, error;
  try {
    result = fn(function () {
      assert.equal(this, undefined);
      ++outer; inner = -1; event('outer'); return outer < rounds;
    }, function () {
      assert.equal(this, undefined);
      ++inner; event('inner'); return inner < innerRounds;
    }, function () {
      assert.equal(this, undefined); event('run');
      if (fails(outer, inner)) throw caught;
    }, function () {
      assert.equal(this, undefined); event('body');
    }, function () {
      assert.equal(this, undefined); event('after');
    });
  } catch (value) {
    assert.equal(value, escaped); error = true;
  }
  assert.equal(result, undefined);
  return { log, error };
}

for (const [index, fn] of functions.entries()) {
  const stop = index % 2 === 1;
  for (const rounds of [0, 1, 3]) {
    for (const innerRounds of [0, 1, 3]) {
      for (const fails of [() => false, () => true, (o, i) => o === 1 && i === 1,
        (o, i) => (o + i) % 2 === 1]) {
        for (const escapeAt of [undefined, 'outer', 'inner', 'body', 'after']) {
          assert.deepEqual(execute(fn, rounds, innerRounds, fails, escapeAt),
            execute((...args) => reference(stop, ...args), rounds, innerRounds, fails, escapeAt));
        }
      }
    }
  }
}

const carriedSource = execFileSync(process.argv[2], ['--outer-carried'], { encoding: 'utf8', timeout: 60000 });
assert.ok(!carriedSource.includes('INCOMPLETE'));
const { functions: carried } = await import(`data:text/javascript;base64,${Buffer.from(carriedSource).toString('base64')}`);
assert.equal(carried.length, 6);
function referenceCarried(stop, initial, nextOuter, nextInner, run, body, after) {
  let x = initial;
  outer: while (nextOuter()) {
    while (nextInner()) {
      try { x = run(x); }
      catch (_) {
        if (stop) break outer;
        continue outer;
      }
      x = body(x);
    }
    x = after(x);
  }
  return x;
}
function executeCarried(fn, initial, updates, fails) {
  let outer = -1, inner = -1, writes = 0, value = initial;
  const log = [];
  function update(name, old) {
    assert.ok(Object.is(old, value));
    log.push([name, outer, inner, writes]);
    if (name === 'run' && fails(outer, inner)) throw {};
    return value = updates[writes++];
  }
  const result = fn(initial, () => {
    ++outer; inner = -1; log.push(['outer', outer]); return outer < 3;
  }, () => {
    ++inner; log.push(['inner', inner]); return inner < 3;
  }, old => update('run', old), old => update('body', old), old => update('after', old));
  assert.ok(Object.is(result, value));
  return { result, log };
}
for (const [index, fn] of carried.entries()) {
  for (const initial of [undefined, null, -0, NaN, 4n, Symbol('initial'), {}]) {
    for (const fails of [() => false, () => true, (o, i) => o === 1 && i === 1,
      (o, i) => (o + i) % 2 === 1]) {
      const updates = Array.from({ length: 32 }, () => ({}));
      assert.deepEqual(executeCarried(fn, initial, updates, fails),
        executeCarried((...args) => referenceCarried(index % 2 === 1, ...args), initial, updates, fails));
    }
  }
}

const nestedSource = execFileSync(process.argv[2], ['--nested-outer'], { encoding: 'utf8', timeout: 60000 });
assert.ok(!nestedSource.includes('INCOMPLETE'));
const { functions: nested } = await import(`data:text/javascript;base64,${Buffer.from(nestedSource).toString('base64')}`);
assert.equal(nested.length, 12);
function referenceNested(stop, nextOuter, nextInner, run, body, after, observe) {
  outer: while (nextOuter()) {
    while (nextInner()) {
      try {
        try { run(); }
        catch (_) {
          if (stop) break outer;
          continue outer;
        }
        body();
      } catch (_) {
        observe();
        if (stop) break outer;
        continue outer;
      }
    }
    after();
  }
}
function referenceHandler(stop, nextOuter, nextInner, run, body, after, skip) {
  outer: while (nextOuter()) {
    try { run(); }
    catch (_) {
      while (nextInner()) {
        if (skip()) {
          if (stop) break outer;
          continue outer;
        }
        body();
      }
    }
    after();
  }
}
function executeNested(fn, enterCatch, skip, failAt, failLate) {
  const log = [], failure = {};
  let outer = -1, inner = -1, escaped = false;
  function event(name) {
    log.push([name, outer, inner]);
    if (name === failAt && (!failLate || outer === 1)) throw failure;
  }
  try {
    assert.equal(fn(function () {
      assert.equal(this, undefined); ++outer; inner = -1;
      event('outer'); return outer < 3;
    }, function () {
      assert.equal(this, undefined); ++inner; event('inner'); return inner < 3;
    }, function () {
      assert.equal(this, undefined); event('run'); if (enterCatch) throw failure;
    }, function () {
      assert.equal(this, undefined); event('body');
    }, function () {
      assert.equal(this, undefined); event('after');
    }, function () {
      assert.equal(this, undefined); event('extra'); return skip(outer, inner);
    }), undefined);
  } catch (error) {
    assert.equal(error, failure); escaped = true;
  }
  return { log, escaped };
}
for (const [index, fn] of nested.entries()) {
  const oracle = index % 4 < 2 ? referenceNested : referenceHandler;
  for (const enterCatch of [false, true]) {
    for (const skip of [() => false, () => true, (o, i) => o === 1 && i === 1]) {
      for (const failAt of [undefined, 'run', 'body', 'extra', 'inner', 'outer', 'after']) {
        for (const failLate of [false, true]) {
          assert.deepEqual(executeNested(fn, enterCatch, skip, failAt, failLate),
            executeNested((...args) => oracle(index % 2 === 1, ...args), enterCatch, skip, failAt, failLate));
        }
      }
    }
  }
}
