import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

// Raw RETURN executes an inline cleanup; its throw is protected by the same
// user catch as the yield. Retain that dispatch, including a second cleanup
// on that catch path, rather than claiming this is a canonical try/finally.
execFileSync(process.argv[2], ['--yield-return-negative'], {timeout: 60_000});
const fixtures = execFileSync(process.argv[2], ['--yield-return-json'],
  {encoding: 'utf8', timeout: 60_000, maxBuffer: 8_000_000}).trim().split('\n').map(line => JSON.parse(line));
assert.equal(fixtures.length, 162);
for (const fixture of fixtures) {
  assert.ok(!fixture.source.includes('INCOMPLETE')); assert.ok(fixture.source.includes('finally'));
  const {capture} = await import(`data:text/javascript;base64,${Buffer.from(`export const capture = ${fixture.source};`).toString('base64')}`);
  let iteration = 0; const saved = [];
  capture(() => iteration++ === 0, () => { if (fixture.catch_parent) throw 'cell'; return 'cell'; }, get => saved.push(get));
  const get = saved[0]; assert.equal(saved.length, 1); assert.equal(get.length, 2);
  if (fixture.different) {
    for (const request of ['next', 'return', 'throw']) for (const workThrows of [false, true]) for (const cleanupThrows of [false, true]) {
      let work = 0, calls = 0; const yielded = {}, payload = {}, workError = {}, cleanupError = {};
      const iterator = get(() => { if (++work === 2 && workThrows) throw workError; return yielded; },
        () => { ++calls; if (cleanupThrows) throw cleanupError; });
      assert.deepEqual(iterator.next(), {value: yielded, done: false});
      const failedReturn = request === 'return' && workThrows;
      if (failedReturn || request === 'throw' || (request === 'next' && cleanupThrows)) assert.throws(() => iterator[request](payload),
        error => error === (cleanupThrows ? cleanupError : failedReturn ? workError : payload));
      else assert.deepEqual(iterator[request](payload), {value: payload, done: true});
      assert.equal(work, request === 'return' ? 2 : 1);
      assert.equal(calls, request === 'return' ? Number(workThrows) : 1);
      assert.deepEqual(iterator.next(), {value: undefined, done: true});
    }
    continue;
  }
  for (const payload of [undefined, null, false, 0, NaN, Symbol('payload'), {}]) {
    for (const request of ['next', 'return', 'throw']) for (const failure of [0, 1, 2]) {
      const yielded = {get then() { assert.fail('sync yield assimilated thenable'); }}, firstError = {}, secondError = {};
      let calls = 0, work = 0;
      const iterator = get(function () { assert.equal(this, undefined); ++work; return yielded; }, function () {
        assert.equal(this, undefined); ++calls;
        if (failure && calls === 1) throw firstError;
        if (failure === 2 && calls === 2) throw secondError;
      });
      assert.equal(work, 0); assert.equal(calls, 0);
      assert.deepEqual(iterator.next(), {value: yielded, done: false});
      assert.equal(work, 1); assert.equal(calls, 0);
      if (request === 'throw' || failure) assert.throws(() => iterator[request](payload),
        error => Object.is(error, failure === 2 && request === 'return' ? secondError : failure ? firstError : payload));
      else assert.deepEqual(iterator[request](payload), {value: payload, done: true});
      assert.equal(calls, failure && request === 'return' ? 2 : 1);
      assert.deepEqual(iterator.next(), {value: undefined, done: true});
    }
    for (const request of ['return', 'throw']) {
      const iterator = get(() => assert.fail('early request ran work'), () => assert.fail('early request ran cleanup'));
      if (request === 'return') assert.deepEqual(iterator.return(payload), {value: payload, done: true});
      else assert.throws(() => iterator.throw(payload), error => Object.is(error, payload));
      assert.deepEqual(iterator.next(), {value: undefined, done: true});
    }
  }
  const a = {}, b = {}, resumed = {}, iterA = get(() => a, () => {}), iterB = get(() => b, () => {});
  assert.deepEqual(iterA.next(), {value: a, done: false}); assert.deepEqual(iterB.next(), {value: b, done: false});
  assert.deepEqual(iterB.return(b), {value: b, done: true}); assert.deepEqual(iterA.next(resumed), {value: resumed, done: true});
}
const uncaught = execFileSync(process.argv[2], ['--yield-return-uncaught-json'],
  {encoding: 'utf8', timeout: 60_000, maxBuffer: 8_000_000}).trim().split('\n').map(line => JSON.parse(line));
assert.equal(uncaught.length, 9);
for (const fixture of uncaught) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  const {capture} = await import(`data:text/javascript;base64,${Buffer.from(`export const capture = ${fixture.source};`).toString('base64')}`);
  let entered = false, get;
  capture(() => !entered && (entered = true), () => { if (fixture.catch_parent) throw 'cell'; }, child => { get = child; });
  for (const request of ['next', 'return', 'throw']) for (const fail of [false, true]) {
    let calls = 0; const yielded = {}, payload = {}, cleanupError = {};
    const iterator = get(() => yielded, () => { ++calls; if (fail) throw cleanupError; });
    assert.deepEqual(iterator.next(), {value: yielded, done: false});
    if (request === 'throw' || fail) assert.throws(() => iterator[request](payload),
      error => error === (request === 'throw' ? payload : cleanupError));
    else assert.deepEqual(iterator[request](payload), {value: payload, done: true});
    assert.equal(calls, request === 'throw' ? 0 : 1);
    assert.deepEqual(iterator.next(), {value: undefined, done: true});
  }
}
const many = execFileSync(process.argv[2], ['--yield-return-many-json'],
  {encoding: 'utf8', timeout: 60_000, maxBuffer: 8_000_000}).trim().split('\n').map(line => JSON.parse(line));
assert.equal(many.length, 6);
for (const fixture of many) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  const {capture} = await import(`data:text/javascript;base64,${Buffer.from(`export const capture = ${fixture.source};`).toString('base64')}`);
  let entered = false, get;
  capture(() => !entered && (entered = true), () => {}, child => { get = child; });
  const yielded = {}, payload = {};
  for (const request of ['next', 'return', 'throw']) {
    let calls = 0; const iterator = get(() => yielded, () => { ++calls; });
    assert.deepEqual(iterator.next(), {value: yielded, done: false});
    if (request === 'throw') assert.throws(() => iterator.throw(payload), error => error === payload);
    else assert.deepEqual(iterator[request](payload), {value: payload, done: true});
    assert.equal(calls, request === 'return' ? fixture.count : 1);
  }
  for (const failAt of [1, 17, fixture.count]) {
    let calls = 0; const error = {}, iterator = get(() => yielded, () => { if (++calls === failAt) throw error; });
    iterator.next(); assert.throws(() => iterator.return(payload), caught => caught === error);
    assert.equal(calls, failAt + 1); // Original user catch still runs its own cleanup.
  }
}
const captured = execFileSync(process.argv[2], ['--yield-return-cell-json'],
  {encoding: 'utf8', timeout: 60_000, maxBuffer: 8_000_000}).trim().split('\n').map(line => JSON.parse(line));
assert.equal(captured.length, 9);
for (const fixture of captured) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  const {capture} = await import(`data:text/javascript;base64,${Buffer.from(`export const capture = ${fixture.source};`).toString('base64')}`);
  let iteration = 0; const saved = [], cells = [{}, {}];
  capture(() => iteration < cells.length, () => { const cell = cells[iteration++]; if (fixture.catch_parent) throw cell; return cell; },
    (get, set) => saved.push({get, set}));
  assert.equal(saved.length, 2);
  for (const stop of [1, 2]) {
    const changed = {}, payload = {}, log = [];
    const a = saved[0].get(() => cells[0], value => log.push(value)), b = saved[1].get(() => cells[1], value => log.push(value));
    assert.deepEqual(a.next(), {value: cells[0], done: false}); assert.deepEqual(b.next(), {value: cells[1], done: false});
    if (stop === 2) { a.next(cells[0]); b.next(cells[1]); }
    saved[0].set(changed);
    assert.deepEqual(b.return(payload), {value: payload, done: true}); assert.deepEqual(a.return(payload), {value: payload, done: true});
    assert.deepEqual(log, [cells[1], changed]);
  }
}
const sites = execFileSync(process.argv[2], ['--yield-return-sites-json'],
  {encoding: 'utf8', timeout: 60_000, maxBuffer: 8_000_000}).trim().split('\n').map(line => JSON.parse(line));
assert.equal(sites.length, 3);
// Doubling straight guarded suspensions must not quadruple indentation bytes.
// This checks the emitted artifact, not just a claimed compact RegionPlan.
const once = sites.find(fixture => fixture.count === 201).source.length;
const twice = sites.find(fixture => fixture.count === 402).source.length;
assert.ok(twice < 3 * once, `continuation output grew superlinearly: ${once} -> ${twice}`);
for (const fixture of sites) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  const {capture} = await import(`data:text/javascript;base64,${Buffer.from(`export const capture = ${fixture.source};`).toString('base64')}`);
  let entered = false, get;
  capture(() => !entered && (entered = true), () => {}, child => { get = child; });
  for (const stop of [1, 17, fixture.count]) for (const request of ['next', 'return', 'throw']) {
    const value = {}, payload = {}; let calls = 0;
    const iterator = get(() => value, () => { ++calls; });
    for (let i = 0; i < stop; ++i) assert.deepEqual(iterator.next(value), {value, done: false});
    if (request === 'throw') assert.throws(() => iterator.throw(payload), error => error === payload);
    else if (request === 'return' || stop === fixture.count) assert.deepEqual(iterator[request](payload), {value: payload, done: true});
    else { assert.deepEqual(iterator.next(payload), {value: payload, done: false}); iterator.return(payload); }
    assert.equal(calls, 1); assert.deepEqual(iterator.next(), {value: undefined, done: true});
  }
}
