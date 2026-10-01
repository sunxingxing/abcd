import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

// Execute only the complete decoded read body, not the incomplete PageA record.
// Mutations and independent identity expectations belong to this test harness.
const source = execFileSync(process.argv[2], [process.argv[3], '--source'], {encoding: 'utf8'});
assert.ok(!source.includes('INCOMPLETE'));
const recovered = await import(`data:text/javascript,${encodeURIComponent(source)}`);
const values = [undefined, null, false, -0, NaN, 2n, Symbol('module'), {}, new Proxy({}, {})];
for (const value of values) {
  recovered.set(value);
  assert.ok(Object.is(recovered.read(), value));
}
console.log(`Verified ${values.length} initialized module values through the shared tree and printer.`);
