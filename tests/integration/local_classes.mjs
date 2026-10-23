// Differential for local classes capturing creator bindings: live values,
// shared mutation across instances and module live bindings.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
let nonce = 0;
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p) + `\n//${nonce++}`).toString('base64'));
const trace = async path => {
  const m = await load(path); const log = [];
  const p = m.param(x => { log.push(['cb', x]); return x * 2; });
  log.push(p.done(3), p.done(4), Object.getOwnPropertyNames(Object.getPrototypeOf(p)));
  const h = m.hoisted(5); log.push(h.m(), h.m());
  const u = m.mutable(1), v = m.mutable(10); log.push(u.m(), u.m(), v.m(), u.m());
  const r = m.moduleRead(); log.push(r.m()); const r2 = m.moduleRead(); log.push(r.m(), r2.m());
  log.push(p.constructor.name, p.constructor.length, typeof p.constructor);
  return log;
};
assert.deepEqual(await trace(emitted), await trace(original));
console.log(JSON.stringify({cases: 1}));
