// Differential: RHS getter/call throws, enumeration order, break.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
let cases = 0;
const settle = f => { try { return ['ok', f()]; } catch (e) { return ['throw', e.constructor.name, e.message]; } };
for (const throwRhs of [false, true]) for (const stopAt of [null, 'x', 'z']) for (const shuffle of [false, true]) {
  const run = m => { const log = [];
    const s = {shuffle() { log.push('shuffle'); return shuffle; }, stop(k) { log.push(['stop', k]); return k === stopAt; }, log(t) { log.push(['log', t]); }, key() { log.push('key'); if (throwRhs) throw new RangeError('key'); return 'specs'; }};
    const o = {get specs() { log.push('get'); if (throwRhs) throw new TypeError('rhs'); return {x: 1, y: 2, z: 3}; }};
    log.push(settle(() => m.specs(o, s))); log.push(settle(() => m.computed(o, s))); return log; };
  assert.deepEqual(run(b), run(a)); cases++;
}
console.log(JSON.stringify({cases}));
