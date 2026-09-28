import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--emit'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!source.includes('INCOMPLETE'));
const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 72);
// Hoisting the cell out of the loop, snapshotting the initial callable, or
// losing the catch write all break these post-return identity assertions.
for (const [index, capture] of functions.entries()) {
const catchOnly = index % 3 === 1, normalParent = index % 3 === 2;
for (const rounds of [0, 1, 2, 5]) {
  for (const failure of ['none', 'all', 'alternating']) {
    if (normalParent && failure !== 'none') continue;
    const values = [undefined, Symbol('cell'), {cell: true}, -0, 3n];
    const getters = [], setters = [], expected = [];
    let next = 0, run = 0;
    const result = capture(function() {
      assert.equal(this, undefined);
      return next++ < rounds;
    }, function() {
      assert.equal(this, undefined);
      const iteration = run++;
      const value = values[iteration];
      const throws = failure === 'all' || (failure === 'alternating' && iteration % 2);
      if (!catchOnly || throws) expected.push(value);
      if (throws) throw value;
      return value;
    }, function(getter, setter) {
      assert.equal(this, undefined);
      assert.equal(typeof getter, 'function');
      assert.equal(typeof setter, 'function');
      assert.equal(getter(), expected[getters.length]);
      getters.push(getter);
      setters.push(setter);
    });
    assert.equal(result, undefined);
    assert.equal(next, rounds + 1);
    assert.equal(run, rounds);
    assert.equal(getters.length, expected.length);
    assert.equal(new Set(getters).size, expected.length);
    // Read in reverse and twice, after every original environment was popped.
    for (let i = getters.length - 1; i >= 0; --i) {
      assert.equal(getters[i](), expected[i]);
      assert.equal(getters[i](), expected[i]);
    }
    const replacements = getters.map((_, iteration) => ({iteration}));
    for (let i = 0; i < setters.length; ++i) {
      assert.equal(setters[i](replacements[i]), undefined);
      for (let j = 0; j < getters.length; ++j)
        assert.equal(getters[j](), j <= i ? replacements[j] : expected[j]);
    }
  }
}
const saved = [], stop = {stop: true}, value = {value: true};
let calls = 0;
assert.throws(() => capture(() => true, () => {
  ++calls;
  if (normalParent) return value;
  throw value;
}, (getter, setter) => {
  saved.push({getter, setter});
  throw stop;
}), error => error === stop);
assert.equal(calls, 1);
assert.equal(saved.length, 1);
assert.equal(saved[0].getter(), value);
saved[0].setter(stop);
assert.equal(saved[0].getter(), stop);
if (normalParent) {
  let collected = false;
  assert.throws(() => capture(() => true, () => { throw value; }, () => { collected = true; }),
    error => error === value);
  assert.equal(collected, false);
}
}
