import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const text = execFileSync(process.argv[2], ['--sources'], {encoding: 'utf8', maxBuffer: 8 * 1024 * 1024});
assert.ok(!text.includes('INCOMPLETE'));
const cases = vm.runInNewContext(`"use strict"; (${text})`);
assert.equal(cases.length, 1698);

function reference(value, consumer, barrier) {
  let key;
  if (consumer === 4) key = value.key;
  const saved = value.probe;
  if (barrier) value.barrier;
  switch (consumer) {
    case 0: value.result = saved; break;
    case 1: value.result = typeof saved; break;
    case 2: value.result = value[saved]; break;
    case 3: value.result = saved == null; break;
    case 4: value[key] = saved; break;
    case 5: value(saved); break;
    case 6: value(saved, saved); break;
    default: assert.fail('fixture consumer');
  }
  return saved;
}

function observe(fn, payloadIndex, failAt, mutate, condition) {
  const trace = [], failure = Symbol('failure'); let events = 0;
  const event = (...parts) => { trace.push(parts); if (++events === failAt) throw failure; };
  const coerce = { [Symbol.toPrimitive](hint) { event('coerce-value', hint); return 'lookup'; } };
  const payloads = [undefined, null, false, true, 0, -0, NaN, Infinity, '', 'lookup', 3n,
    Symbol('key'), coerce, Object.create(null)];
  const saved = payloads[payloadIndex];
  const label = item => item === saved ? 'saved' : typeof item === 'symbol' ? 'symbol' : item;
  const keyLabel = key => typeof key === 'symbol' ? 'symbol-key' : key;
  let value;
  const target = function(...args) {
    assert.equal(this, undefined);
    assert.ok(args.length === 1 || args.length === 2);
    for (const arg of args) assert.ok(Object.is(arg, saved));
    event('invoke', args.length);
    if (mutate) Object.defineProperty(target, 'probe', {get() { assert.fail('snapshot reread'); }, configurable: true});
  };
  Object.defineProperties(target, {
    probe: {configurable: true, get() { event('probe'); return saved; }},
    key: {get() { event('key'); return {
      [Symbol.toPrimitive](hint) { event('coerce-key', hint); return 'result'; },
    }; }},
    barrier: {get() { event('barrier'); return undefined; }},
    result: {get() { event('result-get'); return undefined; }, set(item) {
      assert.equal(this, value); event('result-set', label(item));
      if (mutate) Object.defineProperty(target, 'probe', {get() { assert.fail('snapshot reread'); }, configurable: true});
    }},
    lookup: {get() { event('lookup'); return 'found'; }},
  });
  value = new Proxy(target, {
    get(object, key, receiver) { event('get', keyLabel(key)); return Reflect.get(object, key, receiver); },
    set(object, key, item, receiver) { event('set', keyLabel(key), label(item)); return Reflect.set(object, key, item, receiver); },
    apply(object, receiver, args) { event('apply'); return Reflect.apply(object, receiver, args); },
  });
  let completed = false, thrown;
  try {
    assert.ok(Object.is(fn(value, condition), saved)); completed = true;
  } catch (error) {
    if (error === failure) thrown = 'injected';
    else if (error instanceof TypeError || error?.name === 'TypeError') thrown = 'TypeError';
    else throw error;
  }
  return {trace, completed, thrown};
}

let comparisons = 0;
for (const item of cases) for (const condition of item.selection ? [false, true] : [false])
  for (let payload = 0; payload < 14; ++payload)
  for (const mutate of [false, true]) for (let failAt = 0; failAt <= 10; ++failAt) {
    assert.deepEqual(observe(item.source, payload, failAt, mutate, condition),
      observe(value => reference(value, item.consumer, item.barrier), payload, failAt, mutate, condition));
    ++comparisons;
  }
console.log(`${cases.length} decoded source trees and ${comparisons} independent eager snapshot comparisons passed`);
