import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
// These functions originate in decoded ABC fixtures, through common evaluation,
// tree and semantic checks. No source text supplies the engine's expression.
const source = execFileSync(process.argv[2], ['--sources'], {encoding: 'utf8'});
const generated = vm.runInNewContext(source, {Array() { throw new Error('mutable Array binding'); }});
assert.equal(generated.length, 15);
const reference = [value => value || [], value => value && []];
const object = {[Symbol.toPrimitive]() { throw new Error('unexpected coercion'); }};
const values = [undefined, null, false, true, 0, -0, NaN, '', 'text', 0n, 1n, Symbol('value'), object];
let scenarios = 0;
for (let i = 0; i < reference.length; ++i) for (const value of values) {
  const expected = reference[i](value);
  const actual = generated[i](value);
  if (Array.isArray(expected)) {
    assert.ok(Array.isArray(actual));
    assert.equal(actual.length, 0);
    assert.notEqual(generated[i](value), actual, 'allocation must stay fresh');
  } else assert.ok(Object.is(actual, expected));
  ++scenarios;
}
const getterReference = [value => value || value.probe, value => value && value.probe];
function read(fn, primitive, payload, throwing, withGetter) {
  const events = [], failure = {};
  const value = withGetter ? {get probe() {
    assert.equal(this, value); events.push('get');
    if (throwing) throw failure;
    return payload;
  }} : primitive;
  try {
    const result = fn(value);
    return {events, result: result === value && withGetter ? 'same receiver' : result};
  } catch (error) {
    return {events, error: error === failure ? 'same exception' : error.name};
  }
}
for (let i = 0; i < getterReference.length; ++i) {
  for (const value of values) {
    assert.deepEqual(read(generated[i + 2], value, undefined, false, false),
                     read(getterReference[i], value, undefined, false, false));
    ++scenarios;
  }
  for (const payload of values) for (const throwing of [false, true]) {
    assert.deepEqual(read(generated[i + 2], undefined, payload, throwing, true),
                     read(getterReference[i], undefined, payload, throwing, true));
    ++scenarios;
  }
}
const callReference = [value => value || value.probe(), value => value && value.probe()];
function invoke(fn, payload, fail) {
  const events = [], failure = {};
  const value = {get probe() {
    assert.equal(this, value); events.push('get');
    if (fail === 'get') throw failure;
    return function() {
      assert.equal(this, value); events.push('call');
      if (fail === 'call') throw failure;
      return payload;
    };
  }};
  try {
    const result = fn(value);
    return {events, result: result === value ? 'same receiver' : result};
  } catch (error) {
    assert.equal(error, failure); return {events, error: 'same exception'};
  }
}
for (let i = 0; i < callReference.length; ++i) {
  for (const payload of values) for (const fail of [undefined, 'get', 'call']) {
    assert.deepEqual(invoke(generated[i + 4], payload, fail), invoke(callReference[i], payload, fail));
    ++scenarios;
  }
}
// These bytecode forms evaluate the argument before looking up the method.
// The paired earlier-getter form is separately generated and checked below.
// Inlining its phi argument without moving/owning all preceding effects would
// accidentally swap these two programs, even when ordinary return values agree.
const argumentReference = [
  value => { const argument = value.items || []; return value.probe(argument); },
  value => { const argument = value.items && []; return value.probe(argument); },
];
const earlyReference = [value => value.probe(value.items || []), value => value.probe(value.items && [])];
function argumentCall(fn, payload, fail, mutate, shared = false) {
  const events = [], failure = {}, result = {};
  let generation = 0;
  const value = {
    get items() {
      events.push('items'); assert.equal(this, value);
      if (fail === 'items') throw failure;
      if (mutate) ++generation;
      return payload;
    },
    get probe() {
      events.push('method'); assert.equal(this, value);
      if (fail === 'method') throw failure;
      const selected = generation;
      const method = function(argument, original) {
        assert.equal(arguments.length, shared ? 2 : 1);
        if (shared) assert.ok(Object.is(original, payload));
        assert.equal(this, value);
        events.push(`call:${selected}`);
        events.push(Object.is(argument, payload) ? 'same argument' :
          Array.isArray(argument) && argument.length === 0 ? 'empty array' : 'wrong argument');
        if (fail === 'call') throw failure;
        return result;
      };
      // Source member-call recovery must not consult mutable callee helpers.
      for (const name of ['call', 'apply', 'bind']) Object.defineProperty(method, name, {
        get() { throw new Error(`unexpected ${name}`); },
      });
      return method;
    },
  };
  try {
    assert.equal(fn(value), result);
    return {events, result: 'same result'};
  } catch (error) {
    assert.equal(error, failure);
    return {events, error: 'same exception'};
  }
}
for (let i = 0; i < argumentReference.length; ++i) {
  for (const payload of values) for (const fail of [undefined, 'items', 'method', 'call']) {
    for (const mutate of [false, true]) {
      assert.deepEqual(argumentCall(generated[i + 6], payload, fail, mutate),
        argumentCall(argumentReference[i], payload, fail, mutate));
      ++scenarios;
    }
  }
  // Sensitivity check: the oracle distinguishes both getter exceptions and
  // method replacement; it cannot green-light the wrong evaluation order.
  for (const fail of [undefined, 'items', 'method']) {
    assert.notDeepEqual(argumentCall(earlyReference[i], false, fail, true),
      argumentCall(argumentReference[i], false, fail, true));
    ++scenarios;
  }
}
const sharedReference = [
  value => { const original = value.items; return value.probe(original || [], original); },
  value => { const original = value.items; return value.probe(original && [], original); },
];
for (let i = 0; i < sharedReference.length; ++i) {
  for (const payload of values) for (const fail of [undefined, 'items', 'method', 'call']) {
    assert.deepEqual(argumentCall(generated[i + 8], payload, fail, true, true),
      argumentCall(sharedReference[i], payload, fail, true, true));
    ++scenarios;
  }
}
for (let i = 0; i < earlyReference.length; ++i) {
  for (const payload of values) for (const fail of [undefined, 'items', 'method', 'call']) {
    for (const mutate of [false, true]) {
      assert.deepEqual(argumentCall(generated[i + 10], payload, fail, mutate),
        argumentCall(earlyReference[i], payload, fail, mutate));
      ++scenarios;
    }
  }
}
const optionalReference = value => value?.probe;
for (const value of values) {
  assert.ok(Object.is(generated[12](value), optionalReference(value)));
  ++scenarios;
}
for (const payload of values) for (const throwing of [false, true]) {
  assert.deepEqual(read(generated[12], undefined, payload, throwing, true),
    read(optionalReference, undefined, payload, throwing, true));
  ++scenarios;
}
function chain(fn, first, second, fail) {
  const events = [], failure = {};
  const value = {get probe() {
    events.push('first'); if (fail === 'first') throw failure;
    if (!first) return first;
    return {get probe() { events.push('second'); if (fail === 'second') throw failure; return second; }};
  }};
  try { const result = fn(value); return {events, result}; }
  catch (error) { assert.equal(error, failure); return {events, threw: true}; }
}
for (const first of [undefined, null, false, 0, true]) for (const second of values)
  for (const fail of ['', 'first', 'second']) {
    assert.deepEqual(chain(generated[13], first, second, fail), chain(value => value?.probe?.probe, first, second, fail));
    ++scenarios;
  }
const bufferedReference = value => ({probe: value?.probe});
function buffered(fn, value, throwing = false) {
  const events = [], failure = {};
  const receiver = value === 'getter' ? {get probe() {
    assert.equal(this, receiver); events.push('probe');
    if (throwing) throw failure;
    return object;
  }} : value;
  try {
    const result = fn(receiver), descriptor = Object.getOwnPropertyDescriptor(result, 'probe');
    assert.equal(descriptor.writable, true); assert.equal(descriptor.enumerable, true); assert.equal(descriptor.configurable, true);
    assert.notEqual(result, fn(receiver));
    return {events, value: descriptor.value};
  } catch (error) { assert.equal(error, failure); return {events, threw: true}; }
}
for (const value of [...values, 'getter']) for (const throwing of [false, true]) {
  assert.deepEqual(buffered(generated[14], value, throwing), buffered(bufferedReference, value, throwing));
  ++scenarios;
}
const inherited = vm.runInNewContext(`Object.defineProperty(Object.prototype, 'probe', {
  configurable: true, set() { throw new Error('inherited setter'); }
}); ${source}`);
assert.equal(inherited[14](null).probe, undefined);
assert.equal(inherited[14]({probe: object}).probe, object);
console.log(`Decoded logical branch values, getters and calls: ${scenarios} behavior scenarios passed`);
