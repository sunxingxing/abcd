import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

const source = execFileSync(process.argv[2], ['--emit'], { encoding: 'utf8', timeout: 60000 });
assert.ok(!source.includes('INCOMPLETE'));
const { functions } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 6);
for (const fn of functions.filter((_, i) => i % 2 === 0)) {
  for (const initial of [undefined, null, false, -0, NaN, 3n, Symbol('initial'), {}]) {
    for (const rounds of [0, 1, 2, 5]) {
      for (const failure of ['none', 'next', 'step', 'after']) {
        const log = [];
        const updated = Array.from({ length: rounds }, () => ({}));
        let calls = 0, checks = 0;
        const result = fn(initial, function () {
          assert.equal(this, undefined);
          const after = checks++ % 2 === 1;
          log.push([after ? 'after' : 'next', calls]);
          if (after) {
            if (calls === rounds && failure === 'after') throw {};
            return false; // Ignored audit result must not control the loop.
          }
          if (calls === rounds && failure === 'next') throw {};
          return calls < rounds || failure === 'step';
        }, function (old) {
          assert.equal(this, undefined);
          assert.ok(Object.is(old, calls === 0 ? initial : updated[calls - 1]));
          log.push(['step', calls]);
          if (calls === rounds && failure === 'step') throw {};
          return updated[calls++];
        });
        assert.ok(Object.is(result, rounds === 0 ? initial : updated[rounds - 1]));
        const expected = [];
        for (let i = 0; i < rounds; ++i) expected.push(['next', i], ['step', i], ['after', i + 1]);
        if (failure !== 'after' || rounds === 0) expected.push(['next', rounds]);
        if (failure === 'step') expected.push(['step', rounds]);
        assert.deepEqual(log, expected);
      }
    }
  }
}
for (const fn of functions.filter((_, i) => i % 2 === 1)) {
  for (const rounds of [0, 1, 2, 5]) {
    for (const fails of [() => false, () => true, i => i % 2 === 0]) {
      const initial = {}, updates = Array.from({ length: rounds }, () => ({}));
      const errors = Array.from({ length: rounds }, () => ({}));
      const log = [];
      let check = 0, old = initial;
      const result = fn(initial, function () {
        assert.equal(this, undefined);
        log.push(['next', check]);
        return check++ < rounds;
      }, function (value) {
        assert.equal(this, undefined);
        assert.equal(value, old);
        const i = check - 1;
        log.push(['step', i]);
        if (fails(i)) throw errors[i];
        return old = updates[i];
      }, function (value, error) {
        assert.equal(this, undefined);
        assert.equal(value, old);
        assert.equal(error, errors[check - 1]);
        log.push(['catch', check - 1]);
      });
      const expected = [];
      let expectedValue = initial;
      for (let i = 0; i < rounds; ++i) {
        expected.push(['next', i], ['step', i]);
        if (fails(i)) expected.push(['catch', i]);
        else expectedValue = updates[i];
      }
      expected.push(['next', rounds]);
      assert.equal(result, expectedValue);
      assert.deepEqual(log, expected);
    }
  }
  const handlerError = {};
  let checks = 0, attempts = 0, catches = 0;
  assert.throws(() => fn({}, () => { ++checks; return true; }, () => {
    ++attempts; throw {};
  }, () => { ++catches; throw handlerError; }), e => e === handlerError);
  assert.deepEqual([checks, attempts, catches], [1, 1, 1]);
}
