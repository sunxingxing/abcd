// Differential: each deferred closure observes its own iteration's binding.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
let cases = 0;
for (const items of [[], [1], [1, 2, 3]]) {
  const run = m => { const kept = [];
    m.ofValues(items, f => kept.push(f));
    m.inKeys(Object.fromEntries(items.map(i => ['k' + i, i])), f => kept.push(f));
    return kept.map(f => f()); };
  assert.deepEqual(run(b), run(a)); cases++;
}
console.log(JSON.stringify({cases}));
