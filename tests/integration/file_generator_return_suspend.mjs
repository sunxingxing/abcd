import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

execFileSync(process.argv[2], ['--yield-return-suspend-negative'], {timeout: 60_000});
const fixtures = ['--yield-return-suspend-json', '--yield-return-suspend-pair-json'].flatMap(mode =>
  execFileSync(process.argv[2], [mode], {encoding: 'utf8', timeout: 60_000, maxBuffer: 16_000_000})
    .trim().split('\n').map(JSON.parse));
assert.equal(fixtures.length, 30);
for (const fixture of fixtures) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  const {capture} = await import(`data:text/javascript;base64,${Buffer.from(`export const capture = ${fixture.source};`).toString('base64')}`);
  let entered = false, get, set;
  capture(() => !entered && (entered = true), () => { if (fixture.catch_parent) throw false; return false; },
    (child, setter) => { get = child; set = setter; });
  for (let stop = 1; stop <= fixture.stops; ++stop) for (const request of ['next', 'return', 'throw']) for (const cut of new Set([1, fixture.count])) {
    const events = [], yielded = {}, cleanupYield = {}, resumed = {}, payload = {}, replacement = {};
    const iterator = get(function(...args) {
      assert.equal(this, undefined);
      events.push(['work', ...args]);
      return yielded;
    }, function() {
      assert.equal(this, undefined);
      events.push(['cleanup']);
      return cleanupYield;
    });
    set(false);
    for (let i = 0; i < stop; ++i) assert.deepEqual(iterator.next(yielded), {value: yielded, done: false});
    assert.deepEqual(iterator.return(payload), {value: cleanupYield, done: false});
    for (let i = 1; i < cut; ++i) assert.deepEqual(iterator.next(replacement), {value: cleanupYield, done: false});
    const cleanupEvents = Array.from({length: cut}, () => ['cleanup']);
    assert.deepEqual(events, [['work'], ...cleanupEvents]);
    set(resumed);
    if (request === 'next') for (let i = cut; i < fixture.count; ++i)
      assert.deepEqual(iterator.next(replacement), {value: cleanupYield, done: false});
    if (request === 'throw') assert.throws(() => iterator.throw(replacement), error => error === replacement);
    else assert.deepEqual(iterator[request](replacement), {value: request === 'return' ? replacement : payload, done: true});
    assert.deepEqual(events, request === 'next' ? [['work'], ...Array.from({length: fixture.count}, () => ['cleanup']), ['work', resumed]] :
      request === 'throw' && fixture.caught ? [['work'], ...cleanupEvents, ['cleanup']] : [['work'], ...cleanupEvents]);
    assert.deepEqual(iterator.next(), {value: undefined, done: true});
  }
  for (let stop = 1; stop <= fixture.stops; ++stop) for (const request of ['next', 'throw']) {
    const events = [], yielded = {}, payload = {};
    const iterator = get(() => { events.push('work'); return yielded; }, () => { events.push('cleanup'); });
    for (let i = 0; i < stop; ++i) assert.deepEqual(iterator.next(yielded), {value: yielded, done: false});
    if (request === 'throw') assert.throws(() => iterator.throw(payload), error => error === payload);
    else {
      for (let i = stop; i < fixture.stops; ++i) assert.deepEqual(iterator.next(payload), {value: payload, done: false});
      assert.deepEqual(iterator.next(payload), {value: payload, done: true});
    }
    assert.deepEqual(events, ['work', ...(request === 'next' || fixture.caught ? ['cleanup'] : [])]);
  }
  for (const payload of [undefined, null, false, 0, -0, NaN, 1n, Symbol(), {},
    {then() { assert.fail('synchronous RETURN must not assimilate a thenable'); }}]) {
    const iterator = get(() => 1, () => 2);
    assert.deepEqual(iterator.next(), {value: 1, done: false});
    assert.deepEqual(iterator.return(payload), {value: 2, done: false});
    for (let i = 1; i < fixture.count; ++i) assert.deepEqual(iterator.next({}), {value: 2, done: false});
    const completion = iterator.next({});
    assert.equal(completion.done, true);
    assert.ok(Object.is(completion.value, payload));
  }
  for (const failure of ['cleanup', 'suffix']) for (const catchFailure of [false, true]) {
    const first = {}, second = {}, events = [];
    let calls = 0;
    const iterator = get(function(...args) {
      events.push('work');
      if (args.length) throw first;
      return 1;
    }, () => {
      events.push('cleanup');
      if (++calls > (failure === 'cleanup' ? 1 : fixture.count) && catchFailure) throw second;
      if (failure === 'cleanup') throw first;
      return 2;
    });
    assert.deepEqual(iterator.next(), {value: 1, done: false});
    if (failure === 'suffix') {
      assert.deepEqual(iterator.return({}), {value: 2, done: false});
      for (let i = 1; i < fixture.count; ++i) assert.deepEqual(iterator.next(), {value: 2, done: false});
      assert.throws(() => iterator.next(), error => error === (fixture.caught && catchFailure ? second : first));
    } else assert.throws(() => iterator.return({}), error => error === (fixture.caught && catchFailure ? second : first));
    assert.deepEqual(events, ['work', ...Array(failure === 'suffix' ? fixture.count : 1).fill('cleanup'), ...(failure === 'suffix' ? ['work'] : []), ...(fixture.caught ? ['cleanup'] : [])]);
  }
  for (const request of ['return', 'throw']) {
    let effects = 0; const payload = {};
    const iterator = get(() => ++effects, () => ++effects);
    if (request === 'throw') assert.throws(() => iterator.throw(payload), error => error === payload);
    else assert.deepEqual(iterator.return(payload), {value: payload, done: true});
    assert.equal(effects, 0);
  }
}
