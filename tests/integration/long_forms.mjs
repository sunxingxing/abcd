import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for wide/long instruction forms (debugger statement,
// 16-bit lexical environments, 32-bit jumps): the original and the emitted
// module's `f` are called with the same arguments; a recording Proxy logs
// every property read and call so evaluation order is compared too.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const recorder = log => {
  const node = path => new Proxy(function () {}, {
    get: (_, key) => { log.push(['get', path, String(key)]); return node(`${path}.${String(key)}`); },
    apply: (_, self, args) => { log.push(['call', path, args.map(a => typeof a === 'function' ? 'proxy' : a)]); return args.length; },
  });
  return node('o');
};
const inputs = [[3], [-1], [0, 2], ['x', 1]];
let cases = 0;
const observe = async path => {
  const module = await load(path);
  const results = [];
  for (const args of inputs) {
    const log = [];
    let value;
    try { value = module.f(recorder(log), ...args); } catch (error) { value = ['threw', String(error)]; }
    if (typeof value === 'function') value = 'proxy';
    results.push({args, value, log});
  }
  return results;
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
for (let i = 0; i < inputs.length; ++i) { assert.deepEqual(emitted[i], original[i], JSON.stringify(inputs[i])); ++cases; }
console.log(JSON.stringify({cases}));
