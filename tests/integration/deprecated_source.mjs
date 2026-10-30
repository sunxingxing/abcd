// Differential oracle for constructed deprecated instruction forms. Both
// modules export `f`; results (settled, for promises), thrown errors and
// observable effects (conversions, getters, callee receiver and arguments,
// proxy traps) must agree for every input of the fixture's family.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted, family] = process.argv.slice(2);
const load = path => import('data:text/javascript;base64,' + Buffer.from(readFileSync(path, 'utf8')).toString('base64'));
const a = await load(original), b = await load(emitted);
let trace = [];
// Shared across both sides' freshly built inputs so symbol keys compare equal.
const key = Symbol('s');
const logged = target => new Proxy(target, Object.fromEntries(['get', 'has', 'ownKeys', 'deleteProperty', 'getOwnPropertyDescriptor']
  .map(trap => [trap, (...args) => { trace.push([trap, typeof args[1] === 'symbol' ? 'symbol' : args[1]]); return Reflect[trap](...args); }])));
const unary = () => [0, -0, 1, -1, 2147483647, -2147483648, 1.5, NaN, Infinity, '7', '', 'x', true, null, undefined,
  10n, -1n, {valueOf() { trace.push('valueOf'); return 3; }}, {valueOf() { throw new RangeError('v'); }}, Symbol('s')]
  .map(value => [value]);
const call = () => {
  const recorder = function(...args) { trace.push([this === undefined, this === globalThis, args.length, ...args]); return args.length; };
  const thrower = () => { throw new RangeError('v'); };
  return [[recorder], [recorder, 1], [recorder, 1, 'x'], [recorder, undefined, null, 3n], [recorder, {}, [], 5],
    [thrower, 1, 2, 3], [1, 2, 3, 4], [undefined], [null, 1], [{}, 1, 2], [class C {}, 1], [Math.max, 4, 9, 2]];
};
const named = () => [[{p: 1}], [{get p() { trace.push(['get', this.q]); return 2; }, q: 'q'}], [Object.create({p: 3})],
  [{}], [null], [undefined], [1], ['s'], [logged({p: 4})], [{get p() { throw new RangeError('v'); }}], [[1]]];
const remove = () => [[{x: 1}, 'x'], [{x: 1}, 'y'], [Object.freeze({x: 1}), 'x'], [null, 'x'], [undefined, 'x'],
  [[1, 2], 0], [[1, 2], 'length'], ['s', 0], [1, 'x'], [logged({x: 1}), 'x'],
  [{x: 1}, {toString() { trace.push('key'); return 'x'; }}], [{}, {toString() { throw new RangeError('v'); }}], [{[Symbol.iterator]: 1}, Symbol.iterator],
  [Object.defineProperty({}, Symbol.iterator, {value: 1}), Symbol.iterator]];
const specifiers = () => [['node:path'], ['node:missing-module'], [{toString() { trace.push('specifier'); return 'node:os'; }}],
  [{toString() { throw new RangeError('v'); }}], [Symbol('s')], [undefined]];
const spread = () => [[{x: 1, y: 2}], [null], [undefined], ['ab'], [[1, 2]], [1], [{[key]: 1, z: 3}],
  [{get x() { trace.push('get'); return 5; }}], [logged({x: 1, y: 2})], [{get x() { throw new RangeError('v'); }}],
  [Object.defineProperty({v: 1}, 'hidden', {value: 2, enumerable: false})], [JSON.parse('{"__proto__": {"z": 1}}')]];
const method = () => {
  const o = {p(...args) { trace.push(['p', this === o, ...args]); return args.length; }};
  const getter = {get p() { trace.push('get'); return function(...args) { trace.push(['call', this === getter, ...args]); return 7; }; }};
  return [[o, 1], [getter, 2], [{p: 1}, 3], [{}, 4], [null, 5], [undefined, 6], ['str', 7],
    [{p() { throw new RangeError('v'); }}, 8], [logged({p(x) { return x * 2; }}), 9]];
};
const spreadcall = () => {
  const o = {p(...args) { trace.push(['p', this === o, ...args]); return args.length; }};
  const iterable = {*[Symbol.iterator]() { trace.push('next'); yield 1; trace.push('next'); yield 2; }};
  return [[o, [1, 2]], [o, []], [o, 'ab'], [o, iterable], [o, [, 3]], [o, null], [o, 5], [null, [1]], [{p: 1}, [1]],
    [{get p() { trace.push('get'); return o.p; }}, [4]], [o, {[Symbol.iterator]() { throw new RangeError('v'); }}]];
};
const inputs = {unary, call, named, delete: remove, import: specifiers, spread, method, spreadcall}[family];
assert.ok(inputs, `unknown family ${family}`);
const describe = value => typeof value === 'bigint' ? `${value}n` : Object.is(value, -0) ? '-0' : value;
const observe = async (f, args) => {
  trace = [];
  let output, error, settled;
  try { output = f(...args); } catch (e) { error = [e.name, e.message === 'v' ? 'v' : '']; }
  if (output instanceof Promise) {
    const pending = output;
    output = 'promise';
    settled = await pending.then(value => ['fulfilled', Object.keys(value).sort().join()],
      e => ['rejected', e?.name ?? typeof e, e?.message === 'v' ? 'v' : '']);
  }
  return {output: describe(output), error, settled, trace: trace.slice()};
};
let cases = 0;
for (let i = 0; i < inputs().length; ++i) {
  assert.deepEqual(await observe(b.f, inputs()[i]), await observe(a.f, inputs()[i]), `input ${i}`);
  ++cases;
}
console.log(JSON.stringify({cases}));
