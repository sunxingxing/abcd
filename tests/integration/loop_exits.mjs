import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--emit'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!source.includes('INCOMPLETE'));
const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 12);
for (const [index, fn] of functions.entries()) {
  const stops = index % 2 === 1;
  const carried = index % 4 >= 2;
  for (const skip of ['none', 'all', 'second', 'late']) {
    const log = [];
    let outer = 0, inner = 0;
    const initial = Symbol('initial');
    let current = initial;
    const callbacks = [function() {
      assert.equal(this, undefined);
      assert.ok(log.length < 100, 'wrong continue target must not spin');
      inner = 0; log.push('outer'); return ++outer <= 3;
    }, function() {
      assert.equal(this, undefined);
      assert.ok(log.length < 100, 'wrong continue target must not spin');
      log.push('inner'); return ++inner <= 2;
    }, () => {
      log.push('skip'); return skip === 'all' || (skip === 'second' && outer === 2) ||
        (skip === 'late' && outer === 2 && inner === 2);
    }, old => {
      if (carried) assert.equal(old, current);
      log.push('body'); return current = {body: log.length};
    }, old => {
      if (carried) assert.equal(old, current);
      log.push('after'); return current = {after: log.length};
    }];
    const result = carried ? fn(initial, ...callbacks) : fn(...callbacks);
    assert.equal(result, carried ? current : undefined);
    const full = ['outer', 'inner', 'skip', 'body', 'inner', 'skip', 'body', 'inner', 'after'];
    const early = ['outer', 'inner', 'skip'];
    const late = ['outer', 'inner', 'skip', 'body', 'inner', 'skip'];
    const expected = skip === 'all' ? (stops ? early : [...early, ...early, ...early, 'outer']) :
      skip === 'second' ? (stops ? [...full, ...early] : [...full, ...early, ...full, 'outer']) :
      skip === 'late' ? (stops ? [...full, ...late] : [...full, ...late, ...full, 'outer']) :
      [...full, ...full, ...full, 'outer'];
    assert.deepEqual(log, expected);
  }
}
