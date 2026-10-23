// Differential: arguments identity, count and values across await.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
let cases = 0;
for (const args of [[], [1], [1, 'x', null], [undefined, 2]]) {
  const run = async m => { const log = [];
    const o = {async call(n, xs) { log.push(['call', n, xs.length, [...xs].slice(1)]); return xs[0] === o; }, async m(n, first) { log.push(['m', n, first]); }};
    log.push(await m.forward(o, ...args)); log.push(await m.factory(o)(...args)); return log; };
  assert.deepEqual(await run(b), await run(a)); cases++;
}
console.log(JSON.stringify({cases}));
