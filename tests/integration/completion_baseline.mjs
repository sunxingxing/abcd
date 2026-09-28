import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

// This deliberately verifies the expanded control-flow baseline, NOT finally
// recovery. --finally-return separately requires a real finalizer AST node.
const source = execFileSync(process.argv[2], ['--finally-expanded'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!source.includes('INCOMPLETE'));
const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 3);
for (const fn of functions) {
  for (const pending of [undefined, null, -0, NaN, 3n, Symbol('pending'), {}]) {
    for (const getThrows of [false, true]) for (const cleanupThrows of [false, true]) {
      const log = [], replacement = {}, cleanupError = {};
      let current = pending, result;
      try {
        result = fn(function () {
          assert.equal(this, undefined); log.push('get');
          if (getThrows) throw current;
          return current;
        }, function () {
          assert.equal(this, undefined); log.push('cleanup'); current = replacement;
          if (cleanupThrows) throw cleanupError;
        });
        assert.ok(!getThrows && !cleanupThrows);
        assert.ok(Object.is(result, pending));
      } catch (error) {
        assert.ok(getThrows || cleanupThrows);
        assert.ok(Object.is(error, cleanupThrows ? cleanupError : pending));
      }
      assert.deepEqual(log, ['get', 'cleanup']);
      assert.equal(current, replacement);
    }
  }
}
