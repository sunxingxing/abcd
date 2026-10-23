// Differential: the hoisted closure observes the declared and reassigned cell.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
let cases = 0;
for (const quick of [false, true]) for (const fail of [false, true]) {
  const run = async m => { const log = []; const o = {quick, async run() { log.push('run'); if (fail) throw new Error('x'); }};
    try { log.push(await m.arm(o, log)); } catch (e) { log.push(['rejected', e.message]); } return log; };
  assert.deepEqual(await run(b), await run(a)); cases++;
}
console.log(JSON.stringify({cases}));
