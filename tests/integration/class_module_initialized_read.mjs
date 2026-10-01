import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

// Only the complete decoded reader slice is executed. Original record/class
// owners establish its proof; these declarations and expectations are an oracle.
const source = execFileSync(process.argv[2], [process.argv[3], '--source'], {encoding: 'utf8'});
assert.ok(!source.includes('INCOMPLETE'));
const recovered = await import(`data:text/javascript,${encodeURIComponent(source)}`);
const values = [undefined, null, false, -0, NaN, 2n, Symbol('module'), {}, new Proxy({}, {})];
for (const value of values) {
  recovered.set(value);
  assert.ok(Object.is(recovered.read(), value));
}
assert.equal(recovered.read.length, 1);
for (const mutated of [
  source.replace('return ImmersiveType;', 'return undefined;'),
  source.replace('return ImmersiveType;', 'return WindowInfo;'),
]) {
  assert.notEqual(mutated, source);
  const module = await import(`data:text/javascript,${encodeURIComponent(mutated)}`);
  const expected = {};
  module.set(expected);
  assert.notEqual(module.read(), expected);
}
console.log(`class module initialization: ${values.length} live identity values, 2 valid-syntax mutations`);
