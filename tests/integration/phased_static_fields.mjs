import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for class static fields created by a phased module
// entry (real top-level try/catch) outside any local handler.
// Static initializer side effects, their order and the catch paths are logged.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const failure = new Error('probe failed');
const scenarios = {
  value: tag => `ok:${tag}`,
  error: () => { throw failure; },
  primitive: () => { throw 'plain'; },
};
const encode = v => v === failure ? 'failure' : Array.isArray(v) ? v.map(encode) : v;
let cases = 0;
for (const [name, probe] of Object.entries(scenarios)) {
  const observe = async path => {
    const log = [];
    globalThis.__log = log;
    globalThis.__seed = tag => { log.push(['seed', tag]); return `seed:${tag}`; };
    globalThis.__probe = tag => { log.push(['probe', tag]); return probe(tag); };
    const module = await load(path);
    const statics = {x: module.A.x, y: module.A.y, m: new module.A().m(), own: Object.getOwnPropertyNames(module.A).sort()};
    const other = {list: module.B.list, z: 'z' in module.B, zv: module.B.z, get: new module.B().get(),
                   own: Object.getOwnPropertyNames(module.B).sort()};
    return {statics, other, log: log.map(encode)};
  };
  assert.deepEqual(await observe(emittedPath), await observe(originalPath), name);
  ++cases;
}
console.log(JSON.stringify({cases}));
