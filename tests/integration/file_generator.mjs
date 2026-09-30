import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

execFileSync(process.argv[2], ['--yield-negative'], {timeout: 60_000});
// Dropping user tables, routing injected THROW outside the catch, or using
// pre-suspend cells makes these actual emitted-code comparisons fail.
for (const mode of ['catch', 'catch-capture', 'catch-yield', 'catch-overwrite', 'catch-loop']) {
  const source = execFileSync(process.argv[2], [`--yield-${mode}`], {encoding: 'utf8', timeout: 60_000});
  assert.ok(!source.includes('INCOMPLETE'));
  const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
  assert.equal(functions.length, 9);
  for (const [index, parent] of functions.entries()) {
    const saved = [], cells = [{}, {}]; let iteration = 0;
    parent(() => iteration < cells.length, () => {
      const cell = cells[iteration++];
      if (index % 3 === 1) throw cell;
      return cell;
    }, (get, set) => saved.push({get, set}));
    assert.equal(saved.length, 2);
    const captured = mode === 'catch-capture', catchYield = mode === 'catch-yield';
    for (const payload of [undefined, null, false, 0, NaN, Symbol('injected'), {}]) {
      const yielded = {};
      for (const callbackThrows of [false, true]) {
        const updated = {};
        let calls = 0, expectedCalls = 0;
        saved[0].set(cells[0]);
        const actual = saved[0].get(function () {
          assert.equal(this, undefined); ++calls; saved[0].set(updated);
          if (callbackThrows) throw payload;
          return yielded;
        });
        let expectedCell = cells[0];
        const expected = (function* () {
          try {
            yield (() => {
              ++expectedCalls; expectedCell = updated;
              if (callbackThrows) throw payload;
              return yielded;
            })();
            return expectedCell;
          } catch (error) {
            if (catchYield) yield error;
            return captured || catchYield ? expectedCell : error;
          }
        })();
        const observe = (iterator, op, value) => {
          try { return {threw: false, result: iterator[op](value)}; }
          catch (error) { return {threw: true, error}; }
        };
        for (const [op, value] of [['next', 'ignored'], ['throw', payload], ['next', 'resumed'], ['next']]) {
          const want = observe(expected, op, value), got = observe(actual, op, value);
          assert.equal(got.threw, want.threw);
          if (want.threw) assert.ok(Object.is(got.error, want.error));
          else {
            assert.equal(got.result.done, want.result.done);
            assert.ok(Object.is(got.result.value, want.result.value));
          }
          assert.equal(calls, expectedCalls);
          if (op === 'next' && !got.threw && !got.result.done) {
            const changedWhileSuspended = {};
            saved[0].set(changedWhileSuspended); expectedCell = changedWhileSuspended;
          }
        }
        assert.equal(calls, 1);
        // A sibling activation must not receive the other catch's updated cell.
        const sibling = saved[1].get(() => yielded);
        assert.deepEqual(sibling.next(), {value: yielded, done: false});
        assert.deepEqual(sibling.next(false), {value: cells[1], done: true});
      }
      for (const op of ['return', 'throw']) {
        const early = saved[0].get(() => assert.fail('initial completion entered user try'));
        if (op === 'return') assert.deepEqual(early.return(payload), {value: payload, done: true});
        else {
          let thrown = false;
          try { early.throw(payload); } catch (error) { thrown = true; assert.ok(Object.is(error, payload)); }
          assert.equal(thrown, true);
        }
      }
      const returning = saved[0].get(() => yielded);
      assert.deepEqual(returning.next(), {value: yielded, done: false});
      assert.deepEqual(returning.return(payload), {value: payload, done: true});
      assert.deepEqual(returning.next(), {value: undefined, done: true});
      if (mode === 'catch-loop') {
        for (const terminate of ['next', 'throw', 'return']) {
          let calls = 0;
          const loop = saved[0].get(() => { ++calls; return yielded; });
          assert.deepEqual(loop.next(), {value: yielded, done: false});
          for (let i = 0; i < 3; ++i) assert.deepEqual(loop.next(true), {value: yielded, done: false});
          const changed = {}; saved[0].set(changed);
          const value = terminate === 'next' ? false : payload;
          const result = loop[terminate](value);
          assert.equal(result.done, true);
          assert.ok(Object.is(result.value, terminate === 'next' ? changed : payload));
          assert.equal(calls, 4);
          assert.deepEqual(loop.next(), {value: undefined, done: true});
        }
      }
      if (catchYield) {
        for (const completion of ['return', 'throw']) {
          const inCatch = saved[0].get(() => yielded), second = {};
          assert.deepEqual(inCatch.next(), {value: yielded, done: false});
          const caught = inCatch.throw(payload);
          assert.equal(caught.done, false); assert.ok(Object.is(caught.value, payload));
          if (completion === 'return') assert.deepEqual(inCatch.return(second), {value: second, done: true});
          else assert.throws(() => inCatch.throw(second), error => error === second);
          assert.deepEqual(inCatch.next(), {value: undefined, done: true});
        }
      }
    }
  }
}
const catchManySource = execFileSync(process.argv[2], ['--yield-catch-many'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!catchManySource.includes('INCOMPLETE'));
const {functions: catchMany} = await import(`data:text/javascript;base64,${Buffer.from(catchManySource).toString('base64')}`);
assert.equal(catchMany.length, 3);
for (const [index, parent] of catchMany.entries()) {
  let entered = false, get, set;
  const cell = {}, changed = {}, yielded = {}, error = {}, count = [200, 201, 402][index];
  parent(() => !entered && (entered = true), () => cell, (read, write) => { get = read; set = write; });
  for (const stop of [1, 17, count]) {
    let calls = 0;
    const iterator = get(() => { ++calls; set(changed); return yielded; });
    for (let i = 0; i < stop; ++i) assert.deepEqual(iterator.next(), {value: yielded, done: false});
    assert.deepEqual(iterator.throw(error), {value: error, done: true});
    assert.equal(calls, stop);
    assert.deepEqual(iterator.next(), {value: undefined, done: true});
  }
  let calls = 0;
  const throwing = get(() => { if (++calls === 17) throw error; return yielded; });
  for (let i = 1; i < 17; ++i) assert.deepEqual(throwing.next(), {value: yielded, done: false});
  assert.deepEqual(throwing.next(), {value: error, done: true});
  assert.equal(calls, 17);
  const normal = get(() => yielded);
  for (let i = 0; i < count; ++i) assert.deepEqual(normal.next(), {value: yielded, done: false});
  assert.deepEqual(normal.next(), {value: changed, done: true});
}
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
