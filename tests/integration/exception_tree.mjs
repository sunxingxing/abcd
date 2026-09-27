import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

// Decoded instruction fixtures through the shared draft builder/printer.
// This does not assert a production exception publication receipt exists.
const source = execFileSync(process.argv[2], ['--emit'], { encoding: 'utf8' });
const { functions } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 6);
for (let index = 0; index < functions.length; ++index) {
  const fn = functions[index];
  const log = [];
  const input = (tag, result, fails) => {
    const call = function () {
      if (index % 2 === 1) assert.equal(this, undefined);
      log.push(tag);
      if (fails) throw result;
      return result;
    };
    return index % 2 === 0 ? { get field() { return call(); } } : call;
  };
  for (const value of [undefined, null, false, -0, NaN, 2n, Symbol('value'), {},
    new Proxy({}, { get() { throw new Error('unexpected coercion'); } })]) {
    const updated = {}, normal = {};
    log.length = 0;
    assert.ok(Object.is(fn(value, input('first', {}, true), input('second', normal, false)), value));
    assert.deepEqual(log, ['first']);
    log.length = 0;
    assert.equal(fn(value, input('first', updated, false), input('second', value, true)), updated);
    assert.deepEqual(log, ['first', 'second']);
    log.length = 0;
    assert.equal(fn(value, input('first', updated, false), input('second', normal, false)), normal);
    assert.deepEqual(log, ['first', 'second']);
  }
}
const regionSource = execFileSync(process.argv[3], ['--emit'], { encoding: 'utf8' });
const { regions } = await import(`data:text/javascript;base64,${Buffer.from(regionSource).toString('base64')}`);
assert.equal(regions.length, 9);
for (let index = 0; index < regions.length; index += 3) {
  const nested = regions[index + 2];
  for (const branch of [regions[index], regions[index + 1]]) {
    assert.equal(branch({ field: 0 }), false);
    assert.equal(branch({ field: 1 }), true);
    let reads = 0;
    assert.equal(branch({ get field() { ++reads; throw {}; } }), undefined);
    assert.equal(reads, 1);
  }
  for (const value of [undefined, null, false, -0, NaN, 2n, Symbol('throw'), {}])
    assert.ok(Object.is(nested(value), value));
}
