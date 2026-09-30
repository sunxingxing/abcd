import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

// A width-specific protocol recognizer loses complete functions; accepting any
// conditional jump also changes injected RETURN/THROW dispatch. Exercise both.
const fixtures = execFileSync(process.argv[2], ['--yield-branch-json'],
  {encoding: 'utf8', timeout: 60_000, maxBuffer: 8_000_000}).trim().split('\n').map(line => JSON.parse(line));
assert.equal(fixtures.length, 405);
execFileSync(process.argv[2], ['--yield-branch-negative'], {timeout: 60_000});
for (const fixture of fixtures) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  const {capture} = await import(`data:text/javascript;base64,${Buffer.from(`export const capture = ${fixture.source};`).toString('base64')}`);
  let iteration = 0; const saved = [], cells = [{}, {}];
  capture(() => iteration < cells.length, () => {
    const cell = cells[iteration++]; if (fixture.catch_parent) throw cell; return cell;
  }, (get, set) => saved.push({get, set}));
  assert.equal(saved.length, 2);
  for (const {get, set} of saved) {
    const value = {get then() { assert.fail('sync yield assimilated thenable'); }}, resumed = {}, error = {}, updated = {};
    assert.equal(Object.getPrototypeOf(get), Object.getPrototypeOf(function* () {}));
    assert.equal(get.length, fixture.mode === 'finally' ? 2 : 1);
    const argument = fixture.mode.startsWith('catch') ? function () { assert.equal(this, undefined); return value; } : value;
    if (fixture.mode === 'finally') {
      for (const fails of [false, true]) for (const request of ['next', 'return', 'throw']) {
        const log = [];
        const iterator = get(function () { assert.equal(this, undefined); log.push('work'); if (fails) throw error; return value; },
          function () { assert.equal(this, undefined); log.push('cleanup'); return resumed; });
        assert.deepEqual(log, []);
        assert.deepEqual(iterator.next(), {value: resumed, done: false});
        if (request === 'throw' || (request === 'next' && fails)) assert.throws(() => iterator[request](updated),
          caught => caught === (request === 'throw' ? updated : error));
        else assert.deepEqual(iterator[request](updated), {value: request === 'return' ? updated : value, done: true});
        assert.deepEqual(log, ['work', 'cleanup']);
        assert.deepEqual(iterator.next(), {value: undefined, done: true});
      }
      continue;
    }
    const iterator = get(argument);
    assert.deepEqual(iterator.next('ignored'), {value, done: false});
    set(updated);
    if (fixture.mode === 'catch-loop') {
      for (let i = 0; i < 3; ++i) assert.deepEqual(iterator.next(true), {value, done: false});
      assert.deepEqual(iterator.next(false), {value: updated, done: true});
    } else if (fixture.mode === 'catch-yield') {
      assert.deepEqual(iterator.throw(error), {value: error, done: false});
      assert.deepEqual(iterator.next(resumed), {value: updated, done: true});
    } else if (fixture.mode === 'parameter-cell') {
      const reader = iterator.next(resumed);
      assert.equal(reader.done, true); assert.equal(reader.value(), resumed);
      set(error); assert.equal(reader.value(), resumed);
    } else assert.deepEqual(iterator.next(resumed), {value: resumed, done: true});
    assert.deepEqual(iterator.next(), {value: undefined, done: true});
    for (const initial of [false, true]) for (const request of ['return', 'throw']) {
      const other = get(argument);
      if (!initial) assert.deepEqual(other.next(), {value, done: false});
      if (request === 'return') assert.deepEqual(other.return(error), {value: error, done: true});
      else if (!initial && fixture.mode === 'catch-yield') {
        assert.deepEqual(other.throw(error), {value: error, done: false});
        assert.throws(() => other.throw(resumed), caught => caught === resumed);
      } else if (!initial && fixture.mode === 'catch-loop') assert.deepEqual(other.throw(error), {value: error, done: true});
      else assert.throws(() => other.throw(error), caught => caught === error);
      assert.deepEqual(other.next(), {value: undefined, done: true});
    }
  }
}
