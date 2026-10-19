import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// The generator consumes decoded ABC; this independent reference never enters
// the engine. Capture is an identity link, not a snapshot of the pre-store value.
const reference = value => {
  const read = () => state;
  let state = value;
  return read;
};
const values = [undefined, null, false, true, 0, -0, NaN, 42, '', 'text', 1n, Symbol('value'), {}, () => {}];
let scenarios = 0;
let mutationChecks = 0;
for (const selector of ['--deferred-source', '--named-deferred-source', '--named-deferred-arrow-source']) {
const source = execFileSync(process.argv[2], [selector], {encoding: 'utf8'});
const generated = vm.runInNewContext(source);
if (selector !== '--deferred-source') assert.match(generated.toString(), /\(restoredValue\)/);
for (const value of values) {
  const actual = generated(value), expected = reference(value);
  assert.equal(typeof actual, 'function');
  for (let count = 0; count < 3; ++count) {
    assert.ok(Object.is(actual(), expected()));
    ++scenarios;
  }
  const other = {};
  const second = generated(other);
  assert.notEqual(actual, second);
  assert.equal(second(), other);
  assert.ok(Object.is(actual(), value));
  ++scenarios;
}
if (selector !== '--deferred-source') {
  const store = source.match(/(\$local\d+|\$binding\d+) = restoredValue;/);
  assert.ok(store);
  const changed = source.replace(store[0], `(${store[1]} = null);`);
  assert.notEqual(changed, source);
  new vm.Script(changed);
  const mutated = vm.runInNewContext(changed), token = {};
  assert.notEqual(mutated(token)(), reference(token)()); ++mutationChecks;
}
}
console.log(`Deferred capture: ${scenarios} value identity and independent cell scenarios passed; ${mutationChecks} valid-JS mutations detected`);
