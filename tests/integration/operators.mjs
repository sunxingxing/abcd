// Differential for the operator/call fixture: every export, many operand kinds.
// Traced valueOf/toString make coercion order observable; results and thrown
// error names must agree exactly between original and emitted modules.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
const describe = v => {
  if (typeof v === 'bigint') return 'bigint:' + v;
  if (typeof v === 'symbol') return 'symbol:' + v.description;
  if (typeof v === 'function') return 'function:' + v.name;
  if (Object.is(v, -0)) return '-0';
  if (typeof v === 'number' && Number.isNaN(v)) return 'NaN';
  if (Array.isArray(v)) return v.map(describe);
  if (v && typeof v === 'object') return 'object';
  return v;
};
const call = (m, name, make) => {
  const trace = [];
  const args = make(trace);
  try { return {value: describe(m[name](...args)), trace}; }
  catch (e) { return {error: e.name, trace}; }
};
const traced = (trace, label, value) => ({
  valueOf() { trace.push(label + '.valueOf'); return value; },
  toString() { trace.push(label + '.toString'); return String(value); },
});
const scalars = [0, -0, 1, -1, 3, 31, 33, 2.5, NaN, Infinity, -Infinity, '7', '', 'x', true, false, null, undefined, 5n, -3n];
let cases = 0;
const check = (name, make) => { assert.deepEqual(call(b, name, make), call(a, name, make), name); cases++; };
for (const x of scalars) for (const y of scalars) check('arith', () => [x, y]);
for (const x of scalars) check('arith', t => [traced(t, 'L', x), traced(t, 'R', 2)]);
check('arith', () => [Symbol('s'), 1]);
for (const x of [...scalars, Symbol('s'), {}, [], () => 0]) { check('unary', () => [x]); check('truthy', () => [x]); }
check('unary', t => [traced(t, 'U', 4)]);
const proto = {inherited: 1}, own = Object.create(proto); own.own = 2;
class C {} const c = new C();
for (const k of ['own', 'inherited', 'missing', 0, Symbol('k')]) for (const [o, K] of [[own, Object], [c, C], [[1], Array]]) check('relation', () => [k, o, K]);
check('relation', () => ['x', 1, Object]);
check('relation', () => ['x', {}, 5]);
check('relation', () => ['x', {}, {[Symbol.hasInstance]: () => true}]);
check('relation', t => [traced(t, 'K', 'own'), own, Object]);
check('consts', () => []);
const sum = (...xs) => xs.reduce((p, q) => p + q, 0);
for (const xs of [[], [1], [1, 2, 3], 'abc', new Set([4, 5])]) check('spreadCall', () => [sum, xs]);
check('spreadCall', () => [sum, 5]);
check('spreadCall', () => [5, []]);
check('manyArgs', () => [sum]);
check('manyArgs', () => [function () { return arguments.length; }]);
check('manyArgs', () => [null]);
check('manyThis', () => [{m(...xs) { return [this.tag, ...xs]; }, tag: 'self'}]);
check('manyThis', () => [{}]);
check('manyThis', t => [{get m() { t.push('get m'); return () => 1; }}]);
check('named', () => []);
for (const x of scalars) check('dbg', () => [x]);
console.log(JSON.stringify({cases}));
