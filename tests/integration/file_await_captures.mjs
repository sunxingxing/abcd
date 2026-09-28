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
