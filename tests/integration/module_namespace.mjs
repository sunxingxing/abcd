// Observes one namespace-import module in a fresh process and prints a trace.
// Original and emitted modules each run in their own process: the imported
// data: modules are shared per process, so mutation must not leak between them.
import {readFileSync} from 'node:fs';
const [path, kind] = process.argv.slice(2);
const m = await import('data:text/javascript;base64,' + Buffer.from(readFileSync(path)).toString('base64'));
const trace = [];
const step = (label, f) => {
  try { trace.push([label, f()]); } catch (e) { trace.push([label, 'throw', e.name]); }
};
if (kind === 'basic') {
  step('read', () => m.read());
  step('live-before', () => m.live());
  step('call', () => m.call(5));
  step('live-after', () => m.live());
  step('call-again', () => m.call(2));
  step('keys', () => m.keys());
  step('identity', () => m.whole() === m.whole());
  step('tag', () => Object.prototype.toString.call(m.whole()));
  step('default', () => m.whole().default);
  step('frozen-write', () => { 'use strict'; try { m.whole().value = 1; return 'wrote'; } catch (e) { return e.name; } });
  step('extensible', () => Object.isExtensible(m.whole()));
} else {
  step('sum', () => m.sum());
  step('last', () => m.last().v);
  step('identity', () => m.last() === m.last());
}
console.log(JSON.stringify(trace));
