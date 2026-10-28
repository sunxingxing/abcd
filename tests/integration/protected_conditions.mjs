import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
// Differential oracle: original source vs emitted source, same inputs, same
// observable log (calls, getter reads, results, thrown values, catches).
const original = await import('data:text/javascript;base64,' + Buffer.from(await readFile(process.argv[2])).toString('base64'));
const emitted = await import(pathToFileURL(process.argv[3]));
assert.deepEqual(Object.keys(emitted).sort(), Object.keys(original).sort());
let cases = 0;
const failure = new Error('boom');
async function observe(build) {
  const log = [], ids = new Map();
  // Objects are logged by first-seen identity, never inspected (no getters run).
  const encode = v => v === failure ? 'boom' : v instanceof Error ? ['error', v.constructor.name] :
    typeof v === 'function' ? 'fn' : v !== null && typeof v === 'object' ?
    ['object', ids.has(v) ? ids.get(v) : (ids.set(v, ids.size), ids.size - 1)] : v;
  const note = (...e) => { log.push(e.map(encode)); };
  const {call} = build(note);
  try { note('result', await call()); } catch (e) { note('throw', e); }
  return log;
}
async function same(name, build) {
  const a = await observe(n => build(original[name], n));
  const b = await observe(n => build(emitted[name], n));
  assert.deepEqual(b, a, name);
  ++cases;
}
const values = [0, 1, 2, 'boom'];
const fn = (note, tag, value) => (...args) => {
  note(tag, ...args);
  if (value === 'boom') throw failure;
  return value;
};
// guarded(a, c, d, g, h)
for (const a of values) for (const c of values) for (const d of [0, 1])
  await same('guarded', (f, note) => ({call: () => f(fn(note, 'a', a), fn(note, 'c', c), d,
    fn(note, 'g', 0), fn(note, 'h', 0))}));
// throwingOperand(x, y, g, h) and asyncCompound(x, y, g)
const object = (note, tag, a, t, d, self) => {
  const o = {
    a: fn(note, tag + '.a', a), t: fn(note, tag + '.t', t), i: fn(note, tag + '.i', a),
    get d() { note(tag + '.d'); if (d === 'boom') throw failure; return d === 'self' ? self() : d; },
  };
  return o;
};
for (const a of values) for (const t of values) for (const d of [0, 'boom', 'self']) {
  await same('throwingOperand', (f, note) => {
    let x; const y = object(note, 'y', 0, 0, d, () => x);
    x = object(note, 'x', a, t, 0, () => x);
    return {call: () => f(x, y, fn(note, 'g', 0), fn(note, 'h', 0))};
  });
  for (const same_ of [false, true]) await same('asyncCompound', (f, note) => {
    const x = object(note, 'x', a, t, 0, () => x);
    const y = same_ ? x : object(note, 'y', 0, 0, 0, () => y);
    return {call: () => f(x, y, async () => { note('g'); if (a === 'boom') throw failure; })};
  });
}
// asyncThrowingOperand(xs, y, g)
for (const d of [0, 'boom', 'self']) for (const n of [0, 1, 3])
  await same('asyncThrowingOperand', (f, note) => {
    const xs = [];
    const y = object(note, 'y', 0, 0, d, () => xs[0]);
    for (let i = 0; i < n; ++i) xs.push(object(note, 'x' + i, i % 2, (i + 1) % 3, 0, () => xs[i]));
    return {call: () => f(xs, y, async v => { note('g', xs.indexOf(v)); })};
  });
// nestedPattern(specs, map, fn, log)
const maps = [{}, {a: ['p', 'q']}, {a: 'p'}, {a: 'z', b: 'p'}, {a: 7}];
for (const map of maps) for (const description of ['p', 'z']) for (const mode of ['ok', 'throw', 'reject'])
  await same('nestedPattern', (f, note) => {
    const specs = [{description}, {get description() { note('getter'); if (mode === 'throw') throw failure; return description; }}];
    const callback = async k => { note('fn', k); if (mode === 'reject') throw failure; };
    return {call: () => f(specs, map, callback, m => note('log', m.split('\n')[0]))};
  });
console.log(JSON.stringify({cases}));
