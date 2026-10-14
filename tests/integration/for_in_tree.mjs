// Syntax/recovery-flow test only. The trees come from for_in_tree_test, not
// ABC recovery. This is deliberately separate from the real-es2abc fixture.
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';

const directory = mkdtempSync(join(tmpdir(), 'abcd-for-in-tree-'));
try {
  const prefix = join(directory, 'tree');
  execFileSync(process.argv[2], [prefix]);
  const generated = {};
  for (const mode of ['continue', 'break'])
    generated[mode] = (await import(pathToFileURL(`${prefix}-${mode}.mjs`))).default;
  const oracle = {
    continue(object, visit) { let key = 17; for (key in object) { visit(key); continue; } return key; },
    break(object, visit) { let key = 17; for (key in object) { visit(key); break; } return key; },
  };
  function trace(fn, create, throwing) {
    const events = [];
    const object = create(events);
    try {
      const result = fn(object, key => {
        events.push(['key', key]);
        if (throwing) throw new RangeError();
      });
      events.push(['return', result]);
    } catch (error) { events.push(['throw', error.constructor.name]); }
    return events;
  }
  const objects = [
    () => ({}),
    () => null,
    () => undefined,
    () => ({b: 1, 10: 2, 2: 3, '': 4, a: 5}),
    () => Object.assign(Object.create({inherited: 1, shadow: 2}), {shadow: 3, own: 4}),
    events => Object.defineProperties({}, {
      visible: {enumerable: true, get() { events.push(['unexpected getter']); throw new Error(); }},
      hidden: {value: 2},
    }),
  ];
  for (const mode of ['continue', 'break']) for (const create of objects)
    for (const throwing of [false, true])
      assert.deepEqual(trace(generated[mode], create, throwing), trace(oracle[mode], create, throwing));
  // Exhaustion must not write undefined over a live-out key. The initial value
  // is observable on the zero-iteration path, and the final key after looping.
  assert.equal(generated.continue({}, () => {}), 17);
  assert.equal(generated.continue({first: 1, last: 2}, () => {}), 'last');
  assert.equal(generated.break({first: 1, last: 2}, () => {}), 'first');
} finally { rmSync(directory, {recursive: true, force: true}); }
