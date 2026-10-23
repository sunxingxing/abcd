// Differential for async generators: awaited next/throw/return plans and
// for-await consumption, compared value-by-value with the original.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
const drive = async (it, plan) => {
  const out = [];
  for (const [k, v] of plan) {
    try { const r = await it[k](v); out.push([r.value, r.done]); }
    catch (e) { out.push(['throw', e instanceof Error ? e.message : e]); }
  }
  return out;
};
const plans = [[['next'], ['next'], ['next']], [['next'], ['throw', 5], ['next']], [['next'], ['return', 6], ['next']],
  [['return', 1]], [['throw', 2], ['next']], [['next', 'x'], ['next', 'y'], ['next', 'z']]];
let cases = 0;
for (const [name, args] of [['one', []], ['two', [3]], ['two', [-1]]]) {
  for (const plan of plans) { assert.deepEqual(await drive(b[name](...args), plan), await drive(a[name](...args), plan), name); cases++; }
  const all = async m => { const r = []; for await (const v of m[name](...args)) r.push(v); return r; };
  assert.deepEqual(await all(b), await all(a)); cases++;
}
console.log(JSON.stringify({cases}));
