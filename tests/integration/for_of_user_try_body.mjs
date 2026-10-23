// Differential: values, order, iterator next/return calls and throw kinds.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
const iterable = (log, items) => ({[Symbol.iterator]() { let i = 0; return {
  next() { log.push(['next', i]); return i < items.length ? {value: items[i++], done: false} : {done: true}; },
  return() { log.push('return'); return {}; }}; }});
const sink = (log, failAt) => { let n = 0; return {
  push(x) { log.push(['push', x]); if (++n === failAt) throw new TypeError('push' + n); },
  after(x) { log.push(['after', x]); }, log(t) { log.push(['log', t]); }}; };
let cases = 0;
const settle = async f => { try { return ['ok', await f()]; } catch (e) { return ['throw', e.constructor.name, e.message]; } };
for (const items of [[], [1], [1, 2, 3]]) for (const failAt of [0, 1, 2, 3]) {
  for (const name of ['logged', 'skipping', 'replacing']) {
    const run = async m => { const log = []; log.push(await settle(() => m[name](iterable(log, items), sink(log, failAt)))); return log; };
    assert.deepEqual(await run(b), await run(a), name); cases++;
  }
  for (const name of ['rethrowing', 'tolerant']) {
    const run = async m => { const log = []; const hooks = items.map((x, i) => async () => { log.push(['hook', x]); if (i + 1 === failAt) throw new Error('h' + x); });
      log.push(await settle(() => m[name](iterable(log, hooks), sink(log, 0)))); return log; };
    assert.deepEqual(await run(b), await run(a), name); cases++;
  }
}
console.log(JSON.stringify({cases}));
