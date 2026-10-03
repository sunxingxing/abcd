import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// Independent AST/printer oracle. Decoded ABC input and VM numeric domains
// remain separate obligations; synthetic type facts do not prove those.
const references = [
  (a,b) => new a(b), (a,b,c) => new (a(b))(c),
  (a,b) => new a.ctor(b), (a,b,c) => new (a(b)).ctor(c),
  (a,b,c) => a(b).fn(c), (a,b) => a(b).value,
  (a,b) => (new a(b)).value, (a,b,c) => new (new a(b))(c),
  (a,b,c) => new a(b) + c, (a,b) => (0, a.fn)(b),
  () => (1).value, a => (-a).value,
  (a,b,c) => new (a || b)(c), (a,b,c) => new (a(b)).ctor.ctor(c)
];
const generated = references.map((_, i) => vm.runInNewContext(execFileSync(
  process.argv[2], ['--source', String(i + 30)], {encoding: 'utf8'})));
function observe(fn, mode, factory, behavior, failAt) {
  const trace = [], failure = Symbol('failure'), token = {}, b = {}, c = {};
  let steps = 0, selected, a;
  const value = x => x === b ? 'b' : x === c ? 'c' : x === token ? 'token' :
    x === selected ? 'selected' : x === a ? 'a' : typeof x === 'object' && x !== null ? 'object' : x;
  const event = (...parts) => {trace.push(parts); if (++steps === failAt) throw failure;};
  function Base(arg) {
    event('base', value(arg), new.target === selected ? 'selected' : new.target === Base ? 'base' : 'none');
    if (behavior === 'throw') throw failure;
    if (behavior === 'object') return token;
    return 3;
  }
  selected = mode === 'bound' ? Base.bind(null, b) : Base;
  if (mode === 'proxy') selected = new Proxy(Base, {
    construct(target, args, newTarget) {event('construct-trap', ...args.map(value)); return Reflect.construct(target, args, newTarget);},
    apply(target, receiver, args) {event('apply-trap', receiver === undefined, ...args.map(value)); return Reflect.apply(target, receiver, args);}
  });
  a = factory ? function Factory(arg) {event('factory', value(arg), !!new.target); return selected;} : selected;
  for (const object of new Set([a, selected])) Object.defineProperties(object, {
    ctor: {get() {event('get-ctor'); return selected;}},
    fn: {get() {event('get-fn'); return function(arg) {
      'use strict'; event('method', value(this), value(arg)); return token;
    };}},
    value: {get() {event('get-value'); return token;}},
    [Symbol.toPrimitive]: {value(hint) {event('coerce', hint); return 7;}}
  });
  try {
    const result = fn(a, b, c);
    trace.push(['return', result === token ? 'token' : result === selected ? 'selected' :
      result === undefined ? 'undefined' : typeof result === 'object' && result !== null ?
      ['instance', Object.getPrototypeOf(result) === Base.prototype,
        Object.getPrototypeOf(result) === selected.prototype, Reflect.ownKeys(result)] : result]);
  } catch (error) {
    if (error === failure) trace.push(['throw', 'injected']);
    else {assert.equal(error.name, 'TypeError'); trace.push(['throw', error.name]);}
  }
  return trace;
}
let scenarios = 0;
for (const [i, reference] of references.entries()) for (const mode of ['direct', 'bound', 'proxy'])
  for (const factory of [false, true]) for (const behavior of ['primitive', 'object', 'throw'])
    for (let failAt = 0; failAt <= 8; ++failAt) {
      assert.deepEqual(observe(generated[i], mode, factory, behavior, failAt),
        observe(reference, mode, factory, behavior, failAt), `postfix ${i}/${mode}/${factory}/${behavior}/${failAt}`);
      ++scenarios;
}
for (const count of [1, 2, 200, 201, 402]) {
  const source = execFileSync(process.argv[2], ['--postfix-chain', String(count)], {encoding: 'utf8'});
  const actual = vm.runInNewContext(source);
  const reference = (a,b,c) => {let target = a(b); for (let i = 0; i < count; ++i) target = target.ctor; return new target(c);};
  for (const mode of ['direct', 'bound', 'proxy']) for (const behavior of ['primitive', 'object', 'throw'])
    for (const failAt of [0, 1, count, count + 1, count + 2, count + 3]) {
      assert.deepEqual(observe(actual, mode, true, behavior, failAt), observe(reference, mode, true, behavior, failAt));
      ++scenarios;
    }
}
console.log(`${scenarios} postfix grouping/receiver/constructor/effect/exception scenarios passed`);
