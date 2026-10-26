// Original source is an independent Node oracle; only ABC reaches the engine.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
let cases = 0;
const check = async observe => {
  assert.deepEqual(await observe(b), await observe(a));
  cases++;
};
await check(m => {
  const C = m.make(), D = m.make();
  return [typeof C, C.name, C.length, C !== D,
    Object.getPrototypeOf(new C()) === C.prototype];
});
for (const value of [7, undefined, null, {n: 4}]) await check(m => {
  const h = m.captured(value), read = h.read;
  return [read() === value, read.name, read.length,
    Object.hasOwn(read, 'prototype'), Object.getPrototypeOf(h).read === read];
});
for (const value of [0, 3, 21]) await check(m => {
  const h = m.mutable(value), k = m.mutable(value);
  return [h.next(), h.next(), k.next(), h.constructor !== k.constructor];
});
for (const value of [undefined, 5, {n: 3}, new Error('caught')]) await check(m => {
  const h = m.caught(value), read = h.read;
  return [read() === value, h.constructor !== m.caught(value).constructor];
});
for (const fail of [false, true, 'escape']) await check(m => {
  const sentinel = new Error('callback failure'), log = [];
  let saved;
  const result = m.escaped(12, C => {
    saved = new C(); log.push(['callback', saved.read()]);
    if (fail === true) throw sentinel;
  });
  return [typeof result, result === sentinel,
    typeof result === 'function' ? result() : result.constructor.name,
    saved.read(), log];
});
for (const name of ['before', 'after']) for (const mode of ['ok', 'reject', 'member-throw']) await check(async m => {
  const sentinel = new Error('user failure'), log = [];
  const cb = x => {
    log.push(['callback', x]);
    if (x === 1 && mode === 'reject') return Promise.reject(sentinel);
    if (x === 2 && mode === 'member-throw') throw sentinel;
    return Promise.resolve(x + 10);
  };
  try {
    const h = await m[name](cb); log.push(['created']);
    const on = h.on;
    const value = await on(2); log.push(['member', value]);
    return {kind: 'return', name: on.name, length: on.length, log};
  } catch (e) {
    return {kind: 'throw', error: e.constructor.name, user: e === sentinel,
      message: e === sentinel ? e.message : undefined, log};
  }
});
for (const mode of ['return', 'throw', 'close']) await check(m => {
  const sentinel = new Error('generator failure'), it = m.generated(15);
  const first = it.next(), read = first.value.read;
  const next = mode === 'throw' ? it.throw(sentinel) : mode === 'close' ? it.return(8) : it.next();
  return [first.done, read(), next.done,
    typeof next.value === 'function' ? new next.value().read() : next.value === sentinel ? 'sentinel' : next.value,
    it.next().done];
});
for (const name of ['before', 'after']) await check(async m => {
  const log = [];
  const cb = x => { log.push(x); return x; };
  const [h, k] = await Promise.all([m[name](cb), m[name](cb)]);
  h.on(4); k.on(5);
  return [h.constructor !== k.constructor, log];
});
for (const reject of [false, true]) await check(async m => {
  const sentinel = new Error('module callback failure'), log = [];
  try {
    const h = await m.moduleReader(x => {
      log.push(x);
      return reject ? Promise.reject(sentinel) : x;
    });
    return ['return', h.read(), h.constructor !== (await m.moduleReader(x => x)).constructor, log];
  } catch (e) {
    return ['throw', e.constructor.name, e === sentinel,
      e === sentinel ? e.message : undefined, log];
  }
});
assert.ok(cases >= 20);
console.log(JSON.stringify({cases}));
