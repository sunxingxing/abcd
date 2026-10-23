// Differential over every boolean combination: results, logs and throws.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
let cases = 0;
for (const name of Object.keys(a)) {
  for (let bits = 0; bits < 32; ++bits) {
    const run = m => {
      const log = [];
      const s = {log: t => log.push(t), run() { log.push('run'); if (bits & 16) throw new Error('boom'); return 'ran'; }};
      for (const [i, k] of ['a', 'b', 'c', 'd'].entries()) Object.defineProperty(s, k, {get() { log.push(k); return !!(bits & (1 << i)); }});
      try { log.push(['result', m[name](s)]); } catch (e) { log.push(['throw', e.message]); }
      return log;
    };
    assert.deepEqual(run(b), run(a), name + '/' + bits); cases++;
  }
}
console.log(JSON.stringify({cases}));
