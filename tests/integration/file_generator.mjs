import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

execFileSync(process.argv[2], ['--yield-negative'], {timeout: 60_000});
const callSource = execFileSync(process.argv[2], ['--yield-call'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!callSource.includes('INCOMPLETE'));
const {functions: calls} = await import(`data:text/javascript;base64,${Buffer.from(callSource).toString('base64')}`);
assert.equal(calls.length, 9);
for (const [index, parent] of calls.entries()) {
  let entered = false, get, set;
  const cell = {}, changed = {}, payload = {};
  parent(() => !entered && (entered = true), () => {
    if (index % 3 === 1) throw cell;
    return cell;
  }, (read, write) => { get = read; set = write; });
  for (const early of ['return', 'throw']) {
    const iterator = get(() => assert.fail('generator body ran before next'));
    if (early === 'return') assert.deepEqual(iterator.return(payload), {value: payload, done: true});
    else assert.throws(() => iterator.throw(payload), error => error === payload);
  }
  const log = [];
  const iterator = get(function () {
    assert.equal(this, undefined); log.push('called'); set(changed); return payload;
  });
  assert.deepEqual(log, []);
  assert.deepEqual(iterator.next(), {value: payload, done: false});
  assert.deepEqual(log, ['called']);
  assert.deepEqual(iterator.next(), {value: changed, done: true});
  assert.deepEqual(log, ['called']);
  for (const error of [undefined, null, false, 0, Symbol('call-error'), {}]) {
    let count = 0;
    const throwing = get(() => { ++count; throw error; });
    let thrown = false;
    try { throwing.next(); } catch (actual) { thrown = true; assert.ok(Object.is(actual, error)); }
    assert.equal(thrown, true); assert.equal(count, 1);
    assert.deepEqual(throwing.next(), {value: undefined, done: true});
  }
}
const manySource = execFileSync(process.argv[2], ['--yield-many'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!manySource.includes('INCOMPLETE'));
const {functions: many} = await import(`data:text/javascript;base64,${Buffer.from(manySource).toString('base64')}`);
assert.equal(many.length, 3);
for (const [index, parent] of many.entries()) {
  let entered = false, get, set;
  const cell = {}, changed = {}, yielded = {};
  parent(() => !entered && (entered = true), () => cell, (read, write) => { get = read; set = write; });
  const iterator = get(yielded), count = [200, 201, 402][index];
  for (let i = 0; i < count; ++i) {
    assert.deepEqual(iterator.next(i), {value: yielded, done: false});
    if (i === 100) set(changed);
  }
  assert.deepEqual(iterator.next(), {value: changed, done: true});
}
const loopSource = execFileSync(process.argv[2], ['--yield-loop'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!loopSource.includes('INCOMPLETE'));
const {functions: loops} = await import(`data:text/javascript;base64,${Buffer.from(loopSource).toString('base64')}`);
assert.equal(loops.length, 9);
for (const [index, parent] of loops.entries()) {
  let entered = false, get, set;
  const cell = {}, updated = {}, yielded = {};
  parent(() => !entered && (entered = true), () => {
    if (index % 3 === 1) throw cell;
    return cell;
  }, (read, write) => { get = read; set = write; });
  const iterator = get(yielded);
  assert.deepEqual(iterator.next(), {value: yielded, done: false});
  for (let i = 0; i < 3; ++i) assert.deepEqual(iterator.next(true), {value: yielded, done: false});
  set(updated);
  assert.deepEqual(iterator.next(false), {value: updated, done: true});
  const closed = get(yielded);
  closed.next(); closed.next(true);
  assert.deepEqual(closed.return(cell), {value: cell, done: true});
}
for (const resultMode of [false, true]) {
  const source = execFileSync(process.argv[2], [resultMode ? '--yield-result' : '--yield-capture'],
    {encoding: 'utf8', timeout: 60_000});
  assert.ok(!source.includes('INCOMPLETE'));
  const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
  assert.equal(functions.length, 9);
  for (const [index, parent] of functions.entries()) {
    const saved = [], cells = [{}, Symbol('other-activation')];
    let iteration = 0;
    parent(() => iteration < cells.length, () => {
      const value = cells[iteration++];
      if (index % 3 === 1) throw value;
      return value;
    }, (get, set) => saved.push({get, set}));
    assert.equal(saved.length, 2);
    for (const {get} of saved) {
      assert.equal(get.name, 'get'); assert.equal(get.length, 1);
      assert.throws(() => new get(), TypeError);
      assert.equal(Object.getPrototypeOf(get), Object.getPrototypeOf(function* () {}));
    }
    const first = saved[0].get('first'), second = saved[1].get('second');
    assert.deepEqual(first.next('ignored'), {value: 'first', done: false});
    assert.deepEqual(second.next('ignored'), {value: 'second', done: false});
    const changed = {}; saved[0].set(changed);
    assert.deepEqual(second.next('resumed-2'), {value: resultMode ? 'resumed-2' : cells[1], done: true});
    assert.deepEqual(first.next('resumed-1'), {value: resultMode ? 'resumed-1' : changed, done: true});

    const neverAssimilate = {get then() { assert.fail('synchronous yield must not await its operand'); }};
    for (const payload of [undefined, null, false, 0, NaN, Symbol('payload'), {}, neverAssimilate]) {
      for (const steps of [['next', 'next', 'next'], ['next', 'return', 'next'],
        ['next', 'throw', 'next'], ['return', 'next'], ['throw', 'next'], ['next', 'next', 'throw']]) {
        const cell = {}; saved[0].set(cell);
        const actual = Reflect.apply(saved[0].get, {unrelated: true}, [payload]);
        const expected = (function* (input) { const resumed = yield input; return resultMode ? resumed : cell; })(payload);
        assert.equal(actual[Symbol.iterator](), actual);
        const observe = (iterator, step) => {
          try { return {threw: false, result: iterator[step](payload)}; }
          catch (error) { return {threw: true, error}; }
        };
        for (const step of steps) {
          const want = observe(expected, step), got = observe(actual, step);
          assert.equal(got.threw, want.threw);
          if (want.threw) assert.ok(Object.is(got.error, want.error));
          else {
            assert.equal(got.result.done, want.result.done);
            assert.ok(Object.is(got.result.value, want.result.value));
            assert.deepEqual(Reflect.ownKeys(got.result), ['value', 'done']);
          }
        }
      }
    }
  }
}
