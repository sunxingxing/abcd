import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--await-capture'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!source.includes('INCOMPLETE'));
const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 9);

for (const [index, capture] of functions.entries()) {
  const saved = [], initial = [{}, Symbol('second')];
  let iteration = 0;
  capture(() => iteration < initial.length, () => {
    const value = initial[iteration++];
    if (index % 3 === 1) throw value;
    return value;
  }, (get, set) => saved.push({get, set}));
  assert.equal(saved.length, 2);
  for (const child of saved) {
    assert.equal(child.get.name, 'get');
    assert.equal(child.get.length, 1);
    assert.throws(() => new child.get(), TypeError);
  }
  // Keep both activations pending; mutate one via its sibling. Resumption
  // reads the live cell, and must not conflate distinct loop/catch instances.
  const resumes = [];
  const pending = saved.map(child => child.get(new Promise(resolve => resumes.push(resolve))));
  const changed = {};
  saved[0].set(changed);
  resumes[1](); resumes[0]();
  assert.deepEqual(await Promise.all(pending), [changed, initial[1]]);

  for (const payload of [undefined, null, false, 0, NaN, Symbol('reject'), {}]) {
    let rejected = false;
    const result = await saved[0].get(Promise.reject(payload)).catch(error => {
      rejected = true; return error;
    });
    assert.equal(rejected, true);
    assert.ok(Object.is(result, payload));
    const updated = {};
    const log = [];
    const input = {get then() {
      log.push('get-then');
      return function (resolve) {
        assert.equal(this, input);
        log.push('call-then'); saved[0].set(updated); resolve(payload);
      };
    }};
    const promise = saved[0].get(input);
    log.push('returned');
    assert.equal(await promise, updated);
    assert.deepEqual(log, ['get-then', 'returned', 'call-then']);
    assert.equal(await saved[1].get(), initial[1]);
  }
}

const closureSource = execFileSync(process.argv[2], ['--await-closure'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!closureSource.includes('INCOMPLETE'));
const {functions: closureFamilies} = await import(`data:text/javascript;base64,${Buffer.from(closureSource).toString('base64')}`);
assert.equal(closureFamilies.length, 9);
for (const [index, capture] of closureFamilies.entries()) {
  const saved = [], initial = [{}, Symbol('other')];
  let iteration = 0;
  capture(() => iteration < 2, () => {
    const value = initial[iteration++];
    if (index % 3 === 1) throw value;
    return value;
  }, (get, set) => saved.push({get, set}));
  const resumes = [];
  const pending = saved.map(child => child.get(new Promise(resolve => resumes.push(resolve))));
  const beforeResume = {};
  saved[0].set(beforeResume);
  resumes[1](); resumes[0]();
  const readers = await Promise.all(pending);
  assert.deepEqual(readers.map(read => read()), [beforeResume, initial[1]]);
  for (const read of readers) { assert.equal(read.name, 'read'); assert.equal(read.length, 0); }
  const afterResume = {};
  saved[1].set(afterResume);
  assert.deepEqual(readers.map(read => read()), [beforeResume, afterResume]);
}

const catchSource = execFileSync(process.argv[2], ['--await-catch'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!catchSource.includes('INCOMPLETE'));
const {functions: catchFamilies} = await import(`data:text/javascript;base64,${Buffer.from(catchSource).toString('base64')}`);
assert.equal(catchFamilies.length, 9);
async function observeCatch(fn, makeInput) {
  const log = [];
  const promise = fn(makeInput(log));
  assert.ok(promise instanceof Promise);
  log.push('returned');
  let result;
  const settled = promise.then(value => { result = value; log.push('fulfilled'); });
  queueMicrotask(() => log.push('external-microtask'));
  await settled;
  return {result, log};
}
for (const [index, capture] of catchFamilies.entries()) {
  const saved = [], cell = {};
  let iteration = 0;
  capture(() => iteration++ === 0, () => {
    if (index % 3 === 1) throw cell;
    return cell;
  }, (get, set) => saved.push({get, set}));
  assert.equal(saved.length, 1);
  const oracle = async input => {
    try { await input; } catch (error) { return error; }
    return cell;
  };
  for (const payload of [undefined, null, false, 0, NaN, Symbol('caught'), {}]) {
    for (const makeInput of [() => payload, () => Promise.resolve(payload),
      () => Promise.reject(payload),
      log => ({get then() { log.push('get-then'); throw payload; }}),
      log => {
        const input = {get then() {
          log.push('get-then');
          return function (resolve, reject) {
            assert.equal(this, input); log.push('call-then'); reject(payload);
          };
        }};
        return input;
      }]) {
      const expected = await observeCatch(oracle, makeInput);
      const actual = await observeCatch(saved[0].get, makeInput);
      assert.ok(Object.is(actual.result, expected.result));
      assert.deepEqual(actual.log, expected.log);
    }
  }
}
