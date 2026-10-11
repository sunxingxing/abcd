import assert from 'node:assert/strict';
import {generatedSource} from '../support/source/generated_fixture.mjs';

// Evaluation-only module obligations use real lexical TDZs, not helpers that
// synthesize errors or replace live bindings with captured values.
const source = generatedSource(process.argv[2], ['--emit'], {encoding: 'utf8', maxBuffer: 16000000, timeout: 120000});
const probe = `
export const tdz = methods.map(fn => {
  try { return { returned: fn() }; } catch (thrown) { return { thrown }; }
});
export const propertyTdz = properties.map(fn => {
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
export function property(value, index) { moduleValue = value; return properties[index](); }
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
  assert.deepEqual(Object.keys(result.propertyTdz[i]), [field]);
  assert(result.propertyTdz[i][field] instanceof ReferenceError);
}
for (const value of result.firstFailure) assert.equal(value, result.old);
for (const value of result.secondFailure) assert.equal(value, result.first);
for (const value of [undefined, null, false, -0, NaN, 2n, Symbol('value'), {}, new Proxy({}, {})]) {
  for (const read of result.read(value)) assert(Object.is(read, value));
  for (const read of result.pair({}, value)) assert(Object.is(read, value));
  for (let i = 0; i < result.properties.length; ++i) {
    let reads = 0;
    assert(Object.is(result.property({ get moduleValue() { ++reads; return value; } }, i), value));
    assert.equal(reads, 1);
    const input = { get moduleValue() { ++reads; throw value; } };
    if (i % 2) assert(Object.is(result.property(input, i), value));
    else {
      let raised = false;
      try { result.property(input, i); } catch (error) { raised = true; assert(Object.is(error, value)); }
      assert(raised);
    }
    assert.equal(reads, 2);
  }
}

// These mutations operate only on generated test output. Real lexical TDZ and
// catch snapshots must detect dropping GetValue or using the wrong live unit.
const load = async text => import(`data:text/javascript;base64,${Buffer.from(text + probe).toString('base64')}`);
const noRead = await load(source.replaceAll('moduleValue', 'undefined'));
assert(noRead.tdz.some((item, index) => !(item[index % 2 ? 'returned' : 'thrown'] instanceof ReferenceError)));
const wrongSnapshot = await load(source.replaceAll('firstModule', 'secondModule'));
assert(wrongSnapshot.secondFailure.some(value => value !== wrongSnapshot.first));
console.log('15 module sources: TDZ, live bindings, property effects and catch snapshots; 2 adverse mutants detected');
