import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';
// These cases have explicit numeric/tag proof. Tagged INT32_MIN and dangerous
// NaN encodings are separately refused by decoded-input tests.
const values = [0, 7, -7, 2147483647, -2147483648, -0, Infinity, -Infinity];
for (const [index, value] of values.entries()) {
  const source = execFileSync(process.argv[2], ['--source', String(index)], {encoding: 'utf8'});
  assert.ok(!source.includes('INCOMPLETE'));
  const fn = runInNewContext(source);
  assert.ok(Object.is(fn(), -value));
}
console.log(`Proved legacy negation: ${values.length} native result/signed-zero/boundary scenarios passed`);
