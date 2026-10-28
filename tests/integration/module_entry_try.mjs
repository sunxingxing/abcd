import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for module entries with top-level try/catch: evaluate
// the original and the emitted module afresh per scenario and compare the
// exported values, export behaviour and the observable log.
const [originalPath, emittedPath] = process.argv.slice(2);
const sources = {original: readFileSync(originalPath, 'utf8'), emitted: readFileSync(emittedPath, 'utf8')};
let fresh = 0;
const load = text => import('data:text/javascript;base64,' + Buffer.from(`${text}\n// ${++fresh}`).toString('base64'));
const failure = new Error('probe failed');
const scenarios = {
  value: () => 7,
  error: () => { throw failure; },
  primitive: () => { throw 'plain'; },
  undefinedResult: () => undefined,
};
let cases = 0;
const encode = v => v === failure ? 'failure' : v instanceof Error ? ['error', v.message] : v;
for (const [name, probe] of Object.entries(scenarios)) {
  const observe = async text => {
    globalThis.__entryLog = undefined;
    const calls = [];
    globalThis.__entryProbe = key => { calls.push(key); return probe(); };
    const module = await load(text);
    const exports = Object.keys(module).sort();
    const values = {ready: module.ready, late: encode(module.late), a: encode(module.get('a')), err: encode(module.get('err'))};
    const counted = [module.inc(), module.inc()];
    return {exports, values, counted, calls, log: globalThis.__entryLog.map(encode)};
  };
  assert.deepEqual(await observe(sources.emitted), await observe(sources.original), name);
  ++cases;
}
console.log(JSON.stringify({cases}));
