import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
// These functions originate in decoded ABC fixtures, through common evaluation,
// tree and semantic checks. No source text supplies the engine's expression.
const source = execFileSync(process.argv[2], ['--sources'], {encoding: 'utf8'});
const generated = vm.runInNewContext(source, {Array() { throw new Error('mutable Array binding'); }});
assert.equal(generated.length, 28);
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
// The late method-read variant is intentionally different from native
// value.items({probe: value?.probe}): the argument getter runs first.
const bufferedLateReference = value => {
  const argument = {probe: value?.probe};
  return value.items(argument);
};
function bufferedCall(fn, payload, fail, replace) {
  const events = [], failure = {}, result = {};
  let changed = false;
  const receiver = {
    get probe() {
      assert.equal(this, receiver); events.push('probe');
      if (fail === 'probe') throw failure;
      changed = replace;
      return payload;
    },
    get items() {
      assert.equal(this, receiver); events.push(changed ? 'replacement' : 'method');
      if (fail === 'method') throw failure;
      const selected = changed;
      const callable = function(argument) {
        assert.equal(this, receiver); events.push(selected ? 'replacement call' : 'call');
        const descriptor = Object.getOwnPropertyDescriptor(argument, 'probe');
        assert.equal(descriptor.value, payload);
        assert.equal(descriptor.writable, true);
        assert.equal(descriptor.enumerable, true);
        assert.equal(descriptor.configurable, true);
        if (fail === 'call') throw failure;
        return result;
      };
      for (const key of ['call', 'apply', 'bind']) Object.defineProperty(callable, key, {
        get() { throw new Error('mutable invocation helper'); }
      });
      return callable;
    }
  };
  try { assert.equal(fn(receiver), result); return {events}; }
  catch (error) { assert.equal(error, failure); return {events, threw: true}; }
}
for (const payload of values) for (const fail of ['', 'probe', 'method', 'call'])
  for (const replace of [false, true]) {
    assert.deepEqual(bufferedCall(generated[15], payload, fail, replace),
                     bufferedCall(bufferedLateReference, payload, fail, replace));
    assert.deepEqual(bufferedCall(generated[24], payload, fail, replace),
                     bufferedCall(value => value.items({probe: value?.probe}), payload, fail, replace));
    scenarios += 2;
  }
// Sensitivity check: moving a method read across the argument getter changes
// which callable is selected, even when the receiver itself remains identical.
assert.notDeepEqual(bufferedCall(bufferedLateReference, object, '', true),
                   bufferedCall(value => value.items({probe: value?.probe}), object, '', true));
function conditional(fn, predicate, payload, fail) {
  const events = [], failure = {};
  const receiver = {
    get probe() { assert.equal(this, receiver); events.push('predicate'); if (fail === 'predicate') throw failure; return predicate; },
    get items() { assert.equal(this, receiver); events.push('arm'); if (fail === 'arm') throw failure; return payload; }
  };
  try {
    const result = fn(receiver);
    if (Array.isArray(result)) { assert.equal(result.length, 0); assert.notEqual(result, fn(receiver)); return {events, array: true}; }
    assert.ok(Object.is(result, payload)); return {events, result};
  } catch (error) { assert.equal(error, failure); return {events, threw: true}; }
}
for (const [index, reference] of [[16, value => value.probe ? value.items : []],
                                [17, value => !value.probe ? value.items : []]])
  for (const predicate of values) for (const payload of values) for (const fail of ['', 'predicate', 'arm']) {
    assert.deepEqual(conditional(generated[index], predicate, payload, fail), conditional(reference, predicate, payload, fail));
    ++scenarios;
  }
const conditionalCalls = [value => value.items(value.probe ? value.probe : []), value => {
  const argument = value.probe ? value.probe : []; return value.items(argument);
}];
function selectCall(fn, predicate, payload, fail, mutate) {
  const events = [], failure = {}, returned = {};
  let current = predicate, reads = 0;
  const receiver = {
    get probe() {
      assert.equal(this, receiver); const phase = ++reads === 1 ? 'predicate' : 'arm';
      events.push(phase); if (fail === phase) throw failure;
      return reads === 1 ? current : payload;
    },
    get items() {
      assert.equal(this, receiver); events.push('method'); if (fail === 'method') throw failure;
      if (mutate) current = payload;
      const callable = function(argument) {
        assert.equal(this, receiver); events.push('call'); if (fail === 'call') throw failure;
        if (Array.isArray(argument)) { assert.equal(argument.length, 0); events.push('array argument'); }
        else { assert.ok(Object.is(argument, payload)); events.push('selected argument'); }
        return returned;
      };
      for (const key of ['call', 'apply', 'bind']) Object.defineProperty(callable, key, {
        get() { throw new Error('mutable invocation helper'); }
      });
      return callable;
    }
  };
  try { assert.equal(fn(receiver), returned); return {events}; }
  catch (error) { assert.equal(error, failure); return {events, threw: true}; }
}
for (let i = 0; i < conditionalCalls.length; ++i)
  for (const predicate of values) for (const payload of values)
    for (const fail of ['', 'predicate', 'arm', 'method', 'call']) for (const mutate of [false, true]) {
      assert.deepEqual(selectCall(generated[18 + i], predicate, payload, fail, mutate),
                       selectCall(conditionalCalls[i], predicate, payload, fail, mutate));
      ++scenarios;
    }
const distinctReference = value => value === null ? null : value?.probe;
for (const value of values) {
  assert.ok(Object.is(generated[20](value), distinctReference(value))); ++scenarios;
}
for (const payload of values) for (const throwing of [false, true]) {
  assert.deepEqual(read(generated[20], undefined, payload, throwing, true),
                   read(distinctReference, undefined, payload, throwing, true));
  ++scenarios;
}
const optionalCalls = [value => value.items(value?.probe), value => {
  const argument = value?.probe; return value.items(argument);
}];
function optionalCall(fn, initial, replacement, fail, mutate) {
  const events = [], failure = {};
  let current = initial;
  const receiver = {
    get probe() {
      assert.equal(this, receiver); events.push('probe'); if (fail === 'probe') throw failure;
      return current;
    },
    get items() {
      assert.equal(this, receiver); events.push('method'); if (fail === 'method') throw failure;
      if (mutate) current = replacement;
      const callable = function(argument) {
        assert.equal(this, receiver); events.push('call'); if (fail === 'call') throw failure;
        return argument;
      };
      for (const key of ['call', 'apply', 'bind']) Object.defineProperty(callable, key, {
        get() { throw new Error('mutable invocation helper'); }
      });
      return callable;
    }
  };
  try { return {events, result: fn(receiver)}; }
  catch (error) { assert.equal(error, failure); return {events, threw: true}; }
}
for (let i = 0; i < optionalCalls.length; ++i)
  for (const initial of values) for (const replacement of values)
    for (const fail of ['', 'probe', 'method', 'call']) for (const mutate of [false, true]) {
      assert.deepEqual(optionalCall(generated[21 + i], initial, replacement, fail, mutate),
                       optionalCall(optionalCalls[i], initial, replacement, fail, mutate));
      ++scenarios;
    }
const nestedReference = value => value === undefined ? undefined : value.probe;
for (const value of values) {
  assert.deepEqual(read(generated[23], value, undefined, false, false),
                   read(nestedReference, value, undefined, false, false)); ++scenarios;
}
for (const payload of values) for (const throwing of [false, true]) {
  assert.deepEqual(read(generated[23], undefined, payload, throwing, true),
                   read(nestedReference, undefined, payload, throwing, true)); ++scenarios;
}
function bufferedFields(fn, payloads, fail, replace, duplicate) {
  const events = [], failure = {}, result = {};
  let reads = 0;
  const receiver = {};
  const read = name => {
    assert.equal(++reads <= 2, true);
    events.push(name);
    if (replace) Object.defineProperty(receiver, 'items', {value() { throw new Error('re-read method'); }, configurable: true});
    if (fail === `field${reads}`) throw failure;
    return payloads[reads - 1];
  };
  for (const name of ['probe', 'other']) Object.defineProperty(receiver, name, {
    get() { assert.equal(this, receiver); return read(name); }
  });
  Object.defineProperty(receiver, 'items', {configurable: true, get() {
    assert.equal(this, receiver); events.push('method');
    if (fail === 'method') throw failure;
    const method = function(argument) {
      assert.equal(this, receiver); events.push('call');
      assert.deepEqual(Object.keys(argument), duplicate ? ['probe'] : ['probe', 'other']);
      assert.equal(reads, 2);
      const keys = duplicate ? ['probe'] : ['probe', 'other'];
      for (const [index, name] of keys.entries()) {
        const descriptor = Object.getOwnPropertyDescriptor(argument, name);
        assert.ok(Object.is(descriptor.value, payloads[duplicate ? 1 : index]));
        assert.equal(descriptor.writable && descriptor.enumerable && descriptor.configurable, true);
      }
      if (fail === 'call') throw failure;
      return result;
    };
    for (const name of ['call', 'apply', 'bind']) Object.defineProperty(method, name, {
      get() { throw new Error('mutable invocation helper'); }
    });
    return method;
  }});
  try { assert.equal(fn(receiver), result); return {events}; }
  catch (error) { assert.equal(error, failure); return {events, threw: true}; }
}
for (const duplicate of [false, true]) {
  const reference = duplicate
    ? value => value.items({probe: value?.probe, probe: value?.probe})
    : value => value.items({probe: value?.probe, other: value?.other});
  for (const first of values) for (const second of values)
    for (const fail of ['', 'method', 'field1', 'field2', 'call']) for (const replace of [false, true]) {
      assert.deepEqual(bufferedFields(generated[duplicate ? 26 : 25], [first, second], fail, replace, duplicate),
                       bufferedFields(reference, [first, second], fail, replace, duplicate));
      ++scenarios;
    }
}
function bufferedSuffix(fn, first, second, fail, replace) {
  const events = [], failure = {}, result = {};
  const receiver = {};
  const payload = (primitive, index) => ({[Symbol.toPrimitive](hint) {
    assert.equal(hint, 'default'); events.push(`coerce${index}`);
    if (replace) Object.defineProperty(receiver, 'items', {value() { throw new Error('method reread'); }, configurable: true});
    if (fail === `coerce${index}`) throw failure;
    return primitive;
  }});
  for (const [index, name, primitive] of [[1, 'probe', first], [2, 'other', second]])
    Object.defineProperty(receiver, name, {get() {
      assert.equal(this, receiver); events.push(`field${index}`);
      if (fail === `field${index}`) throw failure;
      return payload(primitive, index);
    }});
  Object.defineProperty(receiver, 'items', {configurable: true, get() {
    assert.equal(this, receiver); events.push('method');
    if (fail === 'method') throw failure;
    const method = function(argument) {
      assert.equal(this, receiver); events.push('call');
      assert.deepEqual(Object.keys(argument), ['probe', 'other']);
      assert.equal(argument.probe, first + 'px' + 'px');
      assert.equal(argument.other, second + 'px' + 'px');
      if (fail === 'call') throw failure;
      return result;
    };
    for (const name of ['call', 'apply', 'bind']) Object.defineProperty(method, name, {
      get() { throw new Error('mutable invocation helper'); }
    });
    return method;
  }});
  try { assert.equal(fn(receiver), result); return {events}; }
  catch (error) {
    if (error === failure) return {events, failure: true};
    // Adding a Symbol to a string throws in both realms; error identity across
    // realms is not compared. No later getter or call may run after that throw.
    assert.equal(error.name, 'TypeError');
    assert.ok(typeof first === 'symbol' || typeof second === 'symbol');
    return {events, error: error.name};
  }
}
const suffixReference = value => value.items({probe: value?.probe + 'px' + 'px', other: value?.other + 'px' + 'px'});
for (const first of values.filter(value => value !== object))
  for (const second of values.filter(value => value !== object))
    for (const fail of ['', 'method', 'field1', 'coerce1', 'field2', 'coerce2', 'call'])
      for (const replace of [false, true]) {
        assert.deepEqual(bufferedSuffix(generated[27], first, second, fail, replace),
                         bufferedSuffix(suffixReference, first, second, fail, replace));
        ++scenarios;
      }
console.log(`Decoded logical branch values, getters and calls: ${scenarios} behavior scenarios passed`);
