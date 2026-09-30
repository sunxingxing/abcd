import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

execFileSync(process.argv[2], ['--yield-return-loop-negative'], {timeout: 60_000});
const overrides = ['--yield-return-loop-override-json', '--yield-return-nested-loop-override-json', '--yield-return-post-loop-override-json'].flatMap(mode => {
  const result = execFileSync(process.argv[2], [mode],
    {encoding: 'utf8', timeout: 60_000, maxBuffer: 16_000_000}).trim().split('\n').map(line => JSON.parse(line));
  assert.equal(result.length, 48);
  return result;
});
for (const fixture of overrides) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  const {capture} = await import(`data:text/javascript;base64,${Buffer.from(`export const capture = ${fixture.source};`).toString('base64')}`);
  let entered = false, get, set;
  capture(() => !entered && (entered = true), () => { if (fixture.catch_parent) throw false; return false; },
    (child, setter) => { get = child; set = setter; });
  for (const stop of [1, 2]) for (const finishAt of [0, 1, 2, fixture.count + 1])
    for (const exiting of [false, true]) for (const failure of [0, 1, 2]) {
      let calls = 0, work = 0; const payload = {}, replacement = {}, firstError = {}, secondError = {};
      const iterator = get(() => { ++work; return payload; }, function () {
        assert.equal(this, undefined);
        ++calls;
        if (calls <= failure) throw calls === 1 ? firstError : secondError;
        if (exiting && calls === finishAt) set(false);
        return exiting || calls < finishAt ? calls % 2 ? false : 0 : replacement;
      });
      set(false);
      for (let i = 0; i < stop; ++i) assert.deepEqual(iterator.next(payload), {value: payload, done: false});
      set(finishAt > 0);
      const runs = finishAt > 0 || fixture.post;
      if (runs && (failure || (!exiting && fixture.throwing))) assert.throws(() => iterator.return(payload),
        error => error === (failure ? failure === 2 && fixture.caught ? secondError : firstError : replacement));
      else assert.deepEqual(iterator.return(payload), {value: !runs || exiting ? payload : replacement, done: true});
      assert.equal(calls, !runs ? 0 : failure ? 1 + Number(fixture.caught) : exiting ?
        Math.ceil(Math.max(1, finishAt) / fixture.count) * fixture.count : Math.max(1, finishAt) + Number(fixture.throwing && fixture.caught));
      assert.equal(work, 1 + Number(fixture.nested && exiting && finishAt > 0 && failure === 0));
      assert.deepEqual(iterator.next(), {value: undefined, done: true});
    }
  if (fixture.nested) {
    for (const stop of [1, 2]) for (const rounds of [1, 2, 7]) {
      let work = 0, calls = 0; const payload = {};
      const iterator = get(function() {
        assert.equal(this, undefined);
        if (++work > 1 && work <= rounds) set(true);
        return payload;
      }, function() {
        assert.equal(this, undefined);
        assert.ok(calls < rounds * fixture.count, 'wrong nested break/continue target must not spin');
        if (++calls % fixture.count === 0) set(false);
        return 0;
      });
      set(false);
      for (let i = 0; i < stop; ++i) assert.deepEqual(iterator.next(payload), {value: payload, done: false});
      set(true);
      assert.deepEqual(iterator.return(payload), {value: payload, done: true});
      assert.equal(calls, rounds * fixture.count);
      assert.equal(work, rounds + 1);
    }
    for (const stop of [1, 2]) for (const cleanupFailure of [false, true]) {
      let work = 0, calls = 0; const payload = {}, workError = {}, cleanupError = {};
      const iterator = get(() => {
        if (++work === 2) throw workError;
        return payload;
      }, () => {
        if (++calls === fixture.count) set(false);
        if (cleanupFailure && calls === fixture.count + 1) throw cleanupError;
        return 0;
      });
      set(false);
      for (let i = 0; i < stop; ++i) assert.deepEqual(iterator.next(payload), {value: payload, done: false});
      set(true);
      assert.throws(() => iterator.return(payload), error => error === (fixture.caught && cleanupFailure ? cleanupError : workError));
      assert.equal(work, 2);
      assert.equal(calls, fixture.count + Number(fixture.caught));
    }
  }
}
const fixtures = execFileSync(process.argv[2], ['--yield-return-loop-json'],
  {encoding: 'utf8', timeout: 60_000, maxBuffer: 16_000_000}).trim().split('\n').map(line => JSON.parse(line));
assert.equal(fixtures.length, 24);
for (const fixture of fixtures) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  const {capture} = await import(`data:text/javascript;base64,${Buffer.from(`export const capture = ${fixture.source};`).toString('base64')}`);
  let entered = false, get, set;
  capture(() => !entered && (entered = true), () => { if (fixture.catch_parent) throw false; return false; },
    (child, setter) => { get = child; set = setter; });
  for (const stop of [1, 2]) for (const rounds of [0, 1, 2, 7])
    for (const request of ['next', 'return', 'throw']) for (const failure of [0, 1, 2]) {
      let work = 0, calls = 0; const yielded = {}, payload = {}, firstError = {}, secondError = {};
      const iterator = get(() => { ++work; return yielded; }, function () {
        assert.equal(this, undefined);
        if (++calls === rounds * fixture.count) set(false);
        if (calls <= failure) throw calls === 1 ? firstError : secondError;
      });
      set(false);
      for (let i = 0; i < stop; ++i) assert.deepEqual(iterator.next(yielded), {value: yielded, done: false});
      assert.equal(work, 1); assert.equal(calls, 0);
      set(rounds > 0);
      if (request === 'next' && stop === 1) {
        assert.deepEqual(iterator.next(payload), {value: payload, done: false});
        assert.equal(calls, 0); continue;
      }
      const runs = request === 'return' ? rounds > 0 : request === 'throw' ? fixture.caught : true;
      if (request === 'throw' || (failure && runs)) assert.throws(() => iterator[request](payload),
        error => error === (!failure || !runs ? payload : failure === 2 && request === 'return' && fixture.caught ? secondError : firstError));
      else assert.deepEqual(iterator[request](payload), {value: payload, done: true});
      assert.equal(calls, !runs ? 0 : request !== 'return' ? 1 : failure ? 1 + Number(fixture.caught) : rounds * fixture.count);
      assert.deepEqual(iterator.next(), {value: undefined, done: true});
    }
  for (const request of ['return', 'throw']) {
    const payload = {}, iterator = get(() => assert.fail('early request ran work'), () => assert.fail('early request ran cleanup'));
    if (request === 'return') assert.deepEqual(iterator.return(payload), {value: payload, done: true});
    else assert.throws(() => iterator.throw(payload), error => error === payload);
  }
}
