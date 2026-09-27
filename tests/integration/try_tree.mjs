import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

// Shared AST/printer execution coverage, not a decoded ABC publication claim.
const source = execFileSync(process.argv[2], ['--emit'], { encoding: 'utf8' });
const { caught, overridden, rethrown, mutableCatch, restored, catchOnly, finallyOnly,
  breakFinally, continueOverride, catchReplacement } = await import(
  `data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const opaque = new Proxy({}, { get() { throw new Error('unexpected coercion'); } });
for (const value of [undefined, null, false, 0, -0, NaN, 1n, 'throw', Symbol('throw'), {}, opaque]) {
  let count = 0;
  assert.ok(Object.is(caught(value, () => { ++count; }), value));
  assert.equal(count, 1);
  assert.equal(mutableCatch(value, () => {}), catchReplacement);
  assert.ok(Object.is(restored(value, () => {}), value));
  assert.ok(Object.is(catchOnly(value), value));
  let finalizations = 0;
  assert.ok(Object.is(breakFinally(value, () => { ++finalizations; }), value));
  assert.equal(finalizations, 1);
  assert.ok(Object.is(continueOverride({}, () => value), value));
  const replacement = {};
  assert.equal(overridden(value, () => replacement), replacement);
  let observed = false;
  try { rethrown(value, () => { ++count; }); }
  catch (error) { observed = true; assert.ok(Object.is(error, value)); }
  assert.ok(observed);
  assert.equal(count, 2);
  observed = false;
  try { caught(value, () => { throw replacement; }); }
  catch (error) { observed = true; assert.equal(error, replacement); }
  assert.ok(observed);
  observed = false;
  try { finallyOnly(value, () => { ++count; }); }
  catch (error) { observed = true; assert.ok(Object.is(error, value)); }
  assert.ok(observed);
  assert.equal(count, 3);
  observed = false;
  try { rethrown(value, () => { throw replacement; }); }
  catch (error) { observed = true; assert.equal(error, replacement); }
  assert.ok(observed);
}
