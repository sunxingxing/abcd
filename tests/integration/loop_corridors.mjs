// Differential for split loop-exit corridors: traced getters and pushes.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
let cases = 0;
for (let n = 0; n < 5; ++n) for (let mask = 0; mask < 64; ++mask) {
  const run = m => { const log = []; let budget = 3;
    const s = {get length() { log.push('len'); return mask & 1; }, get a() { log.push('a'); return budget-- > 0 && (mask & 2); }, get b() { log.push('b'); return mask & 4; }};
    const xs = Array.from({length: n}, (_, i) => (mask >> (i % 3 + 3)) & 1);
    m.f(n ? Object.assign(xs, {valueOf() { return n; }}) : 0, s, {push(v) { log.push(['push', v]); }}); return log; };
  assert.deepEqual(run(b), run(a)); cases++;
}
const isOp = c => '+-*/'.includes(c), isG = c => c === '(' || c === ')', isP = (x, y) => '*/'.includes(x) && '+-'.includes(y);
for (const expr of ['1+2', '(1+2)*3', '1*2+3', '((1))', '1+2*3-4/5', '(1+(2*3))', ')(', '1+(2-3)*4', '']) {
  assert.deepEqual(b.toSuffix([...expr], isOp, isG, isP), a.toSuffix([...expr], isOp, isG, isP), expr); cases++;
}
console.log(JSON.stringify({cases}));
