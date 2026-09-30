import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const fixtures = execFileSync(process.argv[2], ['--yield-delegate-json'],
  {encoding: 'utf8', timeout: 60_000, maxBuffer: 16_000_000}).trim().split('\n').map(JSON.parse);
assert.equal(fixtures.length, 70);
assert.equal(fixtures.filter(fixture => fixture.rejected).length, 26);
for (const fixture of fixtures.filter(fixture => !fixture.rejected)) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  assert.ok(fixture.source.includes('yield*'));
  const {capture} = await import(`data:text/javascript;base64,${Buffer.from(`export const capture = ${fixture.source};`).toString('base64')}`);
  let entered = false, get;
  capture(() => !entered && (entered = true), () => { if (fixture.catch_parent) throw false; return false; }, child => { get = child; });
  const count = fixture.count ?? 1;
  assert.equal(fixture.source.match(/yield\*/g).length, count);
  if (count > 1) {
    // Consecutive source delegates reuse the same thirteen VM temporaries.
    // Starting the next delegate must pass undefined and acquire its iterator
    // lazily; completing/throwing out of a delegate must not start later ones.
    const run = (stop, request, done) => {
      const raws = Array.from({length: count}, () => ({done: false}));
      const resumed = Array.from({length: count}, () => ({})), completed = {}, sent = {}, error = {};
      let started = 0;
      const events = [];
      const iterable = {[Symbol.iterator]() {
        const index = started++; let calls = 0;
        const iterator = {
          next(value) {
            assert.equal(this, iterator); assert.equal(arguments.length, 1);
            assert.equal(value, ++calls === 1 ? undefined : resumed[index]);
            events.push(['next', index]); return calls === 1 ? raws[index] : {done: true, value: completed};
          },
          return(value) { assert.equal(this, iterator); assert.equal(value, sent); events.push(['return', index]);
            return done ? {done: true, value: completed} : raws[index]; },
          throw(value) { assert.equal(this, iterator); assert.equal(value, sent); events.push(['throw', index]); throw error; }
        };
        return iterator;
      }};
      const generator = get(iterable);
      assert.equal(started, 0); assert.equal(generator.next(), raws[0]);
      for (let index = 0; index < count; ++index) {
        if (index === stop && request) {
          const result = generator[request](sent);
          if (request === 'throw' || done) {
            assert.deepEqual(result, {done: true, value: request === 'throw' ? error : completed});
            assert.equal(started, index + 1); return;
          }
          assert.equal(result, raws[index]);
        }
        const result = generator.next(resumed[index]);
        if (index + 1 === count) assert.deepEqual(result, {done: true, value: completed});
        else assert.equal(result, raws[index + 1]);
      }
      assert.equal(started, count);
      assert.equal(events.filter(([kind]) => kind === 'next').length, 2 * count);
    };
    run(-1, undefined, false);
    for (const stop of new Set([0, Math.floor(count / 2), count - 1])) {
      run(stop, 'return', false); run(stop, 'return', true); run(stop, 'throw', true);
    }
    continue;
  }
  const failure = (operation, expected) => {
    if (fixture.caught) {
      const result = operation();
      assert.equal(result.done, true);
      if (expected === TypeError) assert.ok(result.value instanceof TypeError);
      else assert.equal(result.value, expected);
    } else assert.throws(operation, error => expected === TypeError ? error instanceof TypeError : error === expected);
  };
  {
    const events = [], payload = {}, resumed = {}, completion = {};
    let calls = 0;
    const raw = {get done() { events.push('done'); return false; },
      get value() { assert.fail('done:false delegate result value must stay unread'); }, extra: payload};
    const iterator = {get next() {
      events.push('next-get');
      return function(value) {
        assert.equal(this, iterator); assert.equal(arguments.length, 1);
        assert.equal(value, ++calls === 1 ? undefined : resumed); events.push('next-call');
        return calls === 1 ? raw : {get done() { events.push('done'); return true; }, get value() { events.push('value'); return completion; }};
      };
    }};
    const iterable = {get [Symbol.iterator]() {
      events.push('iterator-get');
      return function() { assert.equal(this, iterable); assert.equal(arguments.length, 0); events.push('iterator-call'); return iterator; };
    }};
    const generator = get(iterable);
    assert.deepEqual(events, []);
    assert.equal(generator.next(payload), raw);
    assert.deepEqual(events, ['iterator-get', 'iterator-call', 'next-get', 'next-call', 'done']);
    Object.defineProperty(iterator, 'next', {value() { assert.fail('next method must be cached'); }});
    assert.deepEqual(generator.next(resumed), {value: completion, done: true});
    assert.deepEqual(events, ['iterator-get', 'iterator-call', 'next-get', 'next-call', 'done', 'next-call', 'done', 'value']);
  }
  for (const request of ['return', 'throw']) for (const done of [false, true]) {
    const sent = {}, replacement = {}, resumed = {}, events = [];
    const raw = {done: false, get value() { assert.fail('delegated result identity must be retained'); }};
    const iterator = {
      next(value) { assert.equal(this, iterator); events.push(['next', value]); return events.length === 1 ? raw : {done: true, value: resumed}; },
      get [request]() { events.push(['lookup', request]); return function(value) {
        assert.equal(this, iterator); assert.equal(arguments.length, 1); assert.equal(value, sent); events.push(['call', request]);
        return done ? {done: true, value: replacement} : raw;
      }; },
      [Symbol.iterator]() { return this; }
    };
    const generator = get(iterator);
    assert.equal(generator.next(), raw);
    if (done) assert.deepEqual(generator[request](sent), {value: replacement, done: true});
    else {
      assert.equal(generator[request](sent), raw);
      assert.deepEqual(generator.next(resumed), {value: resumed, done: true});
    }
    assert.deepEqual(events, [['next', undefined], ['lookup', request], ['call', request], ...(!done ? [['next', resumed]] : [])]);
  }
  for (const absent of [undefined, null]) {
    const sent = {}, events = [];
    const iterator = {next() { return {done: false}; },
      get return() { events.push('return'); return absent; }, [Symbol.iterator]() { return this; }};
    const generator = get(iterator); generator.next();
    assert.deepEqual(generator.return(sent), {value: sent, done: true}); assert.deepEqual(events, ['return']);
  }
  for (const absent of [undefined, null]) for (const close of ['absent', 'ok', 'non-object', 'getter-error', 'call-error']) {
    const sent = {}, error = {}, events = [];
    const iterator = {next() { return {done: false}; },
      get throw() { events.push('throw'); return absent; },
      get return() {
        events.push('return'); if (close === 'getter-error') throw error;
        if (close === 'absent') return undefined;
        return function() {
          assert.equal(this, iterator); assert.equal(arguments.length, 0); events.push('close');
          if (close === 'call-error') throw error;
          return close === 'non-object' ? 0 : {get done() { assert.fail('IteratorClose does not read done'); },
            get value() { assert.fail('IteratorClose does not read value'); }};
        };
      }, [Symbol.iterator]() { return this; }};
    const generator = get(iterator); generator.next();
    failure(() => generator.throw(sent), close.endsWith('error') ? error : TypeError);
    assert.deepEqual(events, ['throw', 'return', ...(['ok', 'non-object', 'call-error'].includes(close) ? ['close'] : [])]);
  }
  for (const site of ['iterator-get', 'iterator-call', 'next-get', 'next-call', 'done', 'value', 'throw-get', 'return-get']) {
    const error = {}; let resumed = false;
    const iterator = {
      get next() { if (site === 'next-get') throw error; return function() {
        if (site === 'next-call') throw error;
        return {get done() { if (site === 'done') throw error; return site === 'value'; }, get value() { throw error; }};
      }; },
      get throw() { assert.ok(resumed); throw error; }, get return() { assert.ok(resumed); throw error; }
    };
    const iterable = {get [Symbol.iterator]() {
      if (site === 'iterator-get') throw error;
      return function() { if (site === 'iterator-call') throw error; return iterator; };
    }};
    const generator = get(iterable);
    if (site === 'throw-get' || site === 'return-get') {
      assert.equal(generator.next().done, false); resumed = true;
      failure(() => generator[site === 'throw-get' ? 'throw' : 'return']({}), error);
    } else failure(() => generator.next(), error);
  }
  for (const invalid of [null, undefined, 0, false, 'text', Symbol()]) {
    const generator = get({[Symbol.iterator]() { return {next() { return invalid; }}; }});
    failure(() => generator.next(), TypeError);
    const badIterator = get({[Symbol.iterator]() { return invalid; }});
    failure(() => badIterator.next(), TypeError);
    for (const request of ['return', 'throw']) {
      const generator = get({[Symbol.iterator]() { return this; }, next() { return {done: false}; },
        [request]() { return invalid; }});
      generator.next(); failure(() => generator[request]({}), TypeError);
    }
  }
  for (const method of [0, false, 'text', {}]) for (const request of ['next', 'return', 'throw']) {
    const iterator = {[Symbol.iterator]() { return this; }, next() { return {done: false}; }, [request]: method};
    const generator = get(iterator);
    if (request !== 'next') generator.next();
    failure(() => generator[request]({}), TypeError);
  }
  for (const request of ['return', 'throw']) {
    const sent = {}; let effects = 0;
    const generator = get({get [Symbol.iterator]() { ++effects; throw sent; }});
    if (request === 'return') assert.deepEqual(generator.return(sent), {value: sent, done: true});
    else assert.throws(() => generator.throw(sent), error => error === sent);
    assert.equal(effects, 0);
  }
}
