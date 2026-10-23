// Differential for wide-encoding fixtures (TYPEOF_IMM16, 32-bit conditional jumps).
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
const run = (m, name, args) => { const trace = []; try { return {value: m[name](...args(trace)), trace}; } catch (e) { return {error: e.name, trace}; } };
const object = (trace, prefix, n, throwAt) => new Proxy({}, {get(_, k) {
  if (typeof k !== 'string') return undefined;
  trace.push(k); if (k === throwAt) throw new RangeError(k);
  const i = Number(k.slice(prefix.length)); return Number.isNaN(i) ? undefined : i * 3 + 1;
}});
let cases = 0;
const check = (name, args) => { assert.deepEqual(run(b, name, args), run(a, name, args), name); cases++; };
if (a.wideTypeof) for (const x of [1, 'x', null, undefined, {}, () => 0, Symbol('s'), 1n, true]) {
  check('wideTypeof', t => [object(t, 'p', 300), x]);
  check('wideTypeof', t => [object(t, 'p', 300, 'p150'), x]);
}
for (const name of ['bigIf', 'bigElse']) if (a[name]) for (const c of [true, false, 0, 1, '', 'x', null]) {
  check(name, t => [c, object(t, 'q', 50)]);
  check(name, t => [c, object(t, 'q', 50, 'q7')]);
}
console.log(JSON.stringify({cases}));
