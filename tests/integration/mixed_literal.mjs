import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';

const [tool] = process.argv.slice(2);
const failure = {}, token = {}, marker = {};
const hostile = {[Symbol.toPrimitive]() { assert.fail('no coercion is permitted'); }};
const values = [undefined, null, false, -0, NaN, 1, 2n, Symbol('value'), token, hostile];
const simple = (Ctor, value) => {
  let result;
  for (let i = 0; i < 2; ++i)
    result = value.method({left: value.left,
      right: new Ctor(value.first, value.flag ? value.second : value.third, value.tail).getValue(value.argument),
      bottom: value.bottom});
  return result;
};
const aggregate = (Ctor, value) => {
  let result;
  for (let i = 0; i < 2; ++i)
    result = value.method({left: {payload: value.left},
      right: new Ctor({payload: value.first}, value.flag ? {payload: value.second} : {payload: value.third},
        {payload: value.tail}).getValue(value.argument), bottom: {payload: value.bottom}});
  return result;
};
const sequence = (Ctor, value) => value.method({
  left: value.left,
  right: new Ctor(value.first, value.flag ? value.second : value.third, value.tail).getValue(value.argument),
  bottom: value.bottom,
  left: value.left,
  right: new Ctor(value.first, value.flag ? value.second : value.third, value.tail).getValue(value.argument),
  bottom: value.bottom
});
const aggregateSequence = (Ctor, value) => value.method({
  left: {payload: value.left},
  right: new Ctor({payload: value.first}, value.flag ? {payload: value.second} : {payload: value.third},
    {payload: value.tail}).getValue(value.argument),
  bottom: {payload: value.bottom},
  left: {payload: value.left},
  right: new Ctor({payload: value.first}, value.flag ? {payload: value.second} : {payload: value.third},
    {payload: value.tail}).getValue(value.argument),
  bottom: {payload: value.bottom}
});
function execute(fn, aggregates, flag, options) {
  const events = [], hits = new Map(), objects = new Set(), argumentsSeen = [], receivers = new Set();
  let replaced = false, changedBottom = false;
  const event = name => {
    events.push(name);
    const ordinal = (hits.get(name) ?? 0) + 1; hits.set(name, ordinal);
    if (options.fail === name && options.ordinal === ordinal) throw failure;
  };
  const own = (object, keys) => {
    assert.ok(object !== null && typeof object === 'object' && !objects.has(object)); objects.add(object);
    assert.deepEqual(Object.keys(object), keys);
    for (const key of keys) {
      const descriptor = Object.getOwnPropertyDescriptor(object, key);
      assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
    }
  };
  const decode = object => {
    if (!aggregates) return object;
    own(object, ['payload']); return object.payload;
  };
  const value = {
    get method() {
      event('method'); const replacement = replaced;
      const call = function(object) {
        assert.equal(this, value); event(replacement ? 'replacement-call' : 'call');
        own(object, ['left', 'right', 'bottom']);
        events.push(['fields', decode(object.left), object.right, decode(object.bottom)]);
        return marker;
      };
      Object.defineProperty(call, 'call', {get() { assert.fail('mutable call property'); }});
      return call;
    },
    get left() { event('left'); return token; },
    get first() { event('first'); return token; },
    get flag() { event('flag'); return flag; },
    get second() { event('second'); return token; },
    get third() { event('third'); return undefined; },
    get tail() { event('tail'); return hostile; },
    get argument() { event('argument'); return token; },
    get bottom() { event('bottom'); return changedBottom ? marker : token; }
  };
  class Ctor {
    constructor(...args) {
      assert.equal(new.target, Constructor); assert.ok(!receivers.has(this)); receivers.add(this);
      event('construct');
      const unpacked = args.map(decode); argumentsSeen.push(args);
      events.push(['constructor-args', ...unpacked]);
      if (options.replace) replaced = true;
    }
    get getValue() {
      assert.ok(receivers.has(this)); event('getValue');
      const receiver = this;
      return function(arg) {
        assert.equal(this, receiver); event('invoke'); assert.equal(arg, token);
        if (options.mutate) changedBottom = true;
        // Mutating escaped argument objects must never affect another literal.
        if (aggregates && options.mutate) for (const args of argumentsSeen) for (const object of args) object.payload = marker;
        return token;
      };
    }
  }
  const Constructor = options.proxy ? new Proxy(Ctor, {
    construct(target, args, newTarget) {
      event('construct-trap'); return Reflect.construct(target, args, newTarget);
    },
    get(target, key, receiver) {
      if (key === 'prototype') event('prototype');
      return Reflect.get(target, key, receiver);
    }
  }) : Ctor;
  let result;
  try { result = fn(Constructor, value); } catch (error) { assert.equal(error, failure); result = failure; }
  return {events, result};
}
let scenarios = 0;
for (const aggregates of [false, true]) for (const single of [false, true]) {
  const option = single ? (aggregates ? '--aggregate-sequence-source' : '--sequence-source') :
    (aggregates ? '--aggregate-source' : '--source');
  const source = execFileSync(tool, [option], {encoding: 'utf8'});
  assert.ok(source.includes(' ? ') && !source.includes('if (') && !source.includes('.call('));
  const generated = runInNewContext(source), oracle = single ? (aggregates ? aggregateSequence : sequence) :
    (aggregates ? aggregate : simple);
  for (const flag of values) for (const options of [{}, {replace: true}, {mutate: true}, {replace: true, mutate: true},
    {proxy: true}, {proxy: true, replace: true, mutate: true},
    ...['construct-trap', 'prototype'].flatMap(fail => [1, 2].map(ordinal => ({proxy: true, fail, ordinal}))),
    ...['method', 'left', 'first', 'flag', 'second', 'third', 'tail', 'construct', 'getValue',
      'argument', 'invoke', 'bottom', 'call'].flatMap(fail => [1, 2].map(ordinal => ({fail, ordinal})))]) {
    assert.deepEqual(execute(generated, aggregates, flag, options), execute(oracle, aggregates, flag, options));
    ++scenarios;
  }
}
console.log(`Mixed literal parameters: ${scenarios} independent behavior scenarios passed`);
