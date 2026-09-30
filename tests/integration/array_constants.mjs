import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import vm from 'node:vm';
const run = spawnSync(process.argv[2], ['--source'], { encoding: 'utf8' });
assert.equal(run.status, 0, run.stderr);
assert.doesNotMatch(run.stdout, /INCOMPLETE|\$value/);
const create = vm.runInNewContext(`(${run.stdout})`);
const a = create(), b = create();
assert.ok(Array.isArray(a));
assert.notEqual(a, b);
assert.equal(a.length, 6);
assert.equal(a[0], null);
assert.equal(a[1], true);
assert.equal(a[2], 4294967295);
assert.ok(Object.is(a[3], -0));
assert.equal(a[4], 'hello');
assert.equal(a[5], 1.5);
for (let i = 0; i < a.length; ++i) {
  const d = Object.getOwnPropertyDescriptor(a, i);
  assert.ok(d.writable && d.enumerable && d.configurable);
}
a[4] = 'changed';
assert.equal(b[4], 'hello');
