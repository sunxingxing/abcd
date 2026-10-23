// Differential for for-of bodies whose per-iteration environment is protected
// by a cleanup handler, and for sequential loops: values, order, deferred
// closure captures (fresh binding per iteration), body/getter/iterator throws
// and iterator close.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
const sink = (log, failAt) => {
  const kept = []; let n = 0;
  const step = what => { if (++n === failAt) throw new RangeError(what + n); };
  return {kept, push(v) { log.push(['push', v]); step('push'); },
    keep(f) { log.push(['keep']); kept.push(f); step('keep'); }};
};
const iterable = (log, items, closing) => ({[Symbol.iterator]() {
  let i = 0; log.push('acquire');
  return {next() { log.push(['next', i]); return i < items.length ? {value: items[i++], done: false} : {done: true}; },
    ...(closing && {return() { log.push('return'); return {}; }})};
}});
const items = (log, labels, throwAt) => labels.map((label, i) => ({get label() {
  log.push(['label', i]); if (i === throwAt) throw new TypeError('label' + i); return label;
}}));
const run = (fn, build) => {
  const log = []; const args = build(log); const s = args[args.length - 1];
  try { log.push(['result', fn(...args)]); } catch (e) { log.push(['throw', e.constructor.name, e.message]); }
  log.push(['deferred', s.kept.map(f => f())]);
  return log;
};
let cases = 0;
const check = (name, build) => { assert.deepEqual(run(b[name], build), run(a[name], build), name); cases++; };
for (const closing of [true, false])
  for (const labels of [[], ['x'], ['x', 'y', 'z']])
    for (const fail of [0, 1, 2, 3, 4])
      for (const throwAt of [-1, 0, 1]) {
        check('captured', log => [iterable(log, items(log, labels, throwAt), closing), sink(log, fail)]);
        check('nested', log => [iterable(log, items(log, labels, throwAt), closing), sink(log, fail)]);
        check('sequential', log => [iterable(log, [1, 2], closing), iterable(log, items(log, labels, throwAt), closing), sink(log, fail)]);
      }
for (const name of ['captured', 'nested']) {
  check(name, log => [[{label: 'p'}, {label: 'q'}], sink(log, 0)]);
  check(name, log => [new Set([{label: 's'}]), sink(log, 0)]);
}
console.log(JSON.stringify({cases}));
