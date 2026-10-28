import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for guarded import reads inside try/catch: the original
// and the emitted module import the same data: module, whose `helper` calls a
// probe. Each scenario runs every export and compares results and probe calls.
const [originalPath, emittedPath] = process.argv.slice(2);
const sources = {original: readFileSync(originalPath, 'utf8'), emitted: readFileSync(emittedPath, 'utf8')};
let fresh = 0;
const load = text => import('data:text/javascript;base64,' + Buffer.from(`${text}\n// ${++fresh}`).toString('base64'));
const failure = new Error('probe failed');
const scenarios = {
  value: x => x * 2,
  error: () => { throw failure; },
  primitive: () => { throw 'plain'; },
  undefinedResult: () => undefined,
};
const encode = v => v === failure ? 'failure' : Array.isArray(v) ? v.map(encode) : v;
let cases = 0;
for (const [name, probe] of Object.entries(scenarios)) {
  const observe = async text => {
    const calls = [];
    globalThis.__importProbe = x => { calls.push(x); return probe(x); };
    const module = await load(text);
    const results = {};
    for (const key of ['call', 'alias', 'arithmetic', 'construct'])
      results[key] = [3, -1].map(x => encode(module[key](x)));
    return {exports: Object.keys(module).sort(), results, calls};
  };
  assert.deepEqual(await observe(sources.emitted), await observe(sources.original), name);
  ++cases;
}
console.log(JSON.stringify({cases}));
