// Differential for unguarded local module reads in closures created by
// phased creators (try regions, async, generator): values, live bindings
// (module `var` writes through a closure), call order and errors.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
// Each run gets a fresh module instance (live module state is per instance).
let nonce = 0;
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p) + `\n//${nonce++}`).toString('base64'));
const trace = async (path, failOn) => {
  const m = await load(path); const log = [];
  const sink = {push(v) { log.push(['push', v]); if (v === failOn) throw new Error('sink:' + v); }, async wait() { log.push('wait'); }};
  const handlers = {};
  const source = {on(name, f) { log.push(['on', name]); if (name === failOn) throw new RangeError(name); handlers[name] = f; }};
  const step = async (name, f) => { try { log.push([name, await f()]); } catch (e) { log.push([name, 'throw', e.constructor.name, e.message]); } };
  const c = new m.Controller(sink);
  await step('register', () => c.register(source));
  for (const id of ['en', 'zh', 'en']) await step('subtype', () => handlers.subtype?.(id));
  await step('change', () => handlers.change?.());
  await step('counter', () => m.counter);
  await step('guarded', () => m.guarded(sink)());
  await step('later', async () => (await m.later(sink))());
  await step('steps', () => { const g = m.steps(); const first = g.next(); const done = g.next(); return [first.value, done.done, done.value()]; });
  await step('mode', () => JSON.stringify(m.Mode));
  return log;
};
let cases = 0;
for (const failOn of [undefined, 'change', 'subtype', 'start', 'end', 0, 1]) {
  assert.deepEqual(await trace(emitted, failOn), await trace(original, failOn), String(failOn)); cases++;
}
console.log(JSON.stringify({cases}));
