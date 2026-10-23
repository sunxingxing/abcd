// Differential: object-literal method shorthand keeps captures, receivers,
// key order, names, lengths, non-constructability and async/generator kinds.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
const shape = f => [f.name, f.length, typeof f, Object.hasOwn(f, 'prototype'),
  (() => { try { new f(); return 'constructed'; } catch (e) { return e.constructor.name; } })()];
let cases = 0;
const check = async observe => { assert.deepEqual(await observe(b), await observe(a)); cases++; };
await check(m => { const o = m.make(5); return [Object.keys(o), o.add(1), o.self(), shape(o.add), shape(o.self)]; });
for (const ok of [true, false]) await check(m => {
  const log = []; const h = m.capture(v => log.push(['resolve', v]), v => log.push(['reject', v]));
  h.onResult({ok, v: 7}); return [log, shape(h.onResult)];
});
for (const list of [[11, 12], null]) await check(m => m.guarded(list));
await check(m => [m.plain.m(3, 4), m.plain.n, Object.keys(m.plain), shape(m.plain.m)]);
await check(async m => [await m.asyncs.fetch(1), m.asyncs.fetch.constructor.name, shape(m.asyncs.fetch).slice(0, 4)]);
await check(m => [[...m.gens.range(4)], m.gens.range.constructor.name, m.gens.range.name, m.gens.range.length]);
await check(m => { const o = m.nested(9); return [o.outer().inner(), shape(o.outer), shape(o.outer().inner)]; });
console.log(JSON.stringify({cases}));
