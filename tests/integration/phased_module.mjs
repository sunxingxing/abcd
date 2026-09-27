import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

// Evaluation-only module obligations use real lexical TDZs, not helpers that
// synthesize errors or replace live bindings with captured values.
const source = execFileSync(process.argv[2], ['--emit'], { encoding: 'utf8' });
const probe = `
export const tdz = methods.map(fn => {
  try { return { returned: fn() }; } catch (thrown) { return { thrown }; }
});
export const old = {};
export const firstFailure = snapshots.map(fn => fn(old));
export const first = {};
let firstModule = first;
export const secondFailure = snapshots.map(fn => fn(old));
let secondModule;
let moduleValue;
export function read(value) { moduleValue = value; return methods.map(fn => fn()); }
export function pair(first, second) {
  firstModule = first; secondModule = second;
  return snapshots.map(fn => fn(old));
}
`;
const result = await import(`data:text/javascript;base64,${Buffer.from(source + probe).toString('base64')}`);
assert.equal(result.methods.length, 6);
assert.equal(result.snapshots.length, 3);
for (let i = 0; i < result.tdz.length; ++i) {
  const field = i % 2 === 0 ? 'thrown' : 'returned';
  assert.deepEqual(Object.keys(result.tdz[i]), [field]);
  assert(result.tdz[i][field] instanceof ReferenceError);
}
for (const value of result.firstFailure) assert.equal(value, result.old);
for (const value of result.secondFailure) assert.equal(value, result.first);
for (const value of [undefined, null, false, -0, NaN, 2n, Symbol('value'), {}, new Proxy({}, {})]) {
  for (const read of result.read(value)) assert(Object.is(read, value));
  for (const read of result.pair({}, value)) assert(Object.is(read, value));
}
