import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const fixtures = execFileSync(process.argv[2], ['--async-generator-prefix-json'],
  {encoding: 'utf8', timeout: 60_000, maxBuffer: 16_000_000}).trim().split('\n').map(JSON.parse);
assert.equal(fixtures.length, 22);
assert.equal(fixtures.filter(fixture => fixture.rejected).length, 10);
for (const fixture of fixtures.filter(fixture => !fixture.rejected)) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  assert.ok(fixture.source.includes('async function*'));
  const {capture} = await import(`data:text/javascript;base64,${Buffer.from(`export const capture = ${fixture.source};`).toString('base64')}`);
  const cells = [{}, {}], saved = []; let entered = 0;
  capture(() => entered < cells.length, () => {
    const cell = cells[entered++]; if (fixture.catch_parent) throw cell; return cell;
  }, (get, set) => saved.push({get, set}));
  assert.equal(saved.length, 2);
  for (const [index, {get, set}] of saved.entries()) {
    assert.equal(get.name, 'get'); assert.equal(get.length, 1);
    assert.equal(Object.getPrototypeOf(get), Object.getPrototypeOf(async function* () {}));
    assert.ok(Object.hasOwn(get, 'prototype')); assert.throws(() => Reflect.construct(get, []), TypeError);
    const values = Array.from({length: fixture.count + 1}, () => ({})), observed = [];
    const discarded = {get then() { assert.fail('a discarded callback result must not be awaited'); }};
    const generator = get(function(value) {
      assert.equal(this, undefined); assert.equal(arguments.length, 1);
      observed.push(value); set(values[observed.length]); return discarded;
    });
    assert.equal(Object.getPrototypeOf(generator), get.prototype);
    assert.deepEqual(observed, []); set(values[0]);
    const first = generator.next({get then() { assert.fail('initial next payload must stay ignored'); }});
    const after = generator.next();
    assert.deepEqual(await first, {done: true, value: undefined});
    assert.deepEqual(await after, {done: true, value: undefined});
    assert.equal(observed.length, fixture.count);
    observed.forEach((value, at) => assert.equal(value, values[at]));

    // A pending return on the completed iterator owns the request queue;
    // later next/throw requests must wait and never restart the source body.
    let ready, release; const waiting = new Promise(resolve => { ready = resolve; });
    const payload = {}, thrown = {}; let thenCalls = 0, laterSettled = false;
    const pending = generator.return({then(resolve) { ++thenCalls; release = resolve; ready(); }});
    const later = generator.next().then(result => { laterSettled = true; return result; });
    const rejected = generator.throw(thrown).then(() => assert.fail('throw must reject'), error => error);
    await waiting; await Promise.resolve(); assert.equal(laterSettled, false);
    assert.equal(thenCalls, 1); release(payload);
    const returned = await pending;
    assert.deepEqual(returned, {done: true, value: payload}); assert.equal(returned.value, payload);
    assert.deepEqual(await later, {done: true, value: undefined}); assert.equal(await rejected, thrown);
    assert.equal(observed.length, fixture.count);
    observed.forEach((value, at) => assert.equal(value, values[at]));

    for (const stop of new Set([0, fixture.count - 1])) {
      const error = {get then() { assert.fail('throw reasons must not be assimilated'); }};
      let calls = 0;
      const broken = get(() => { if (calls++ === stop) throw error; });
      const failed = broken.next(); const queued = broken.next();
      // assert.rejects returns the caught reason from an async helper and thus
      // probes its then getter. A rejection observer must return undefined
      // here so the harness does not assimilate the tested abrupt value.
      await failed.then(() => assert.fail('callback throw must reject'), actual => { assert.equal(actual, error); });
      assert.deepEqual(await queued, {done: true, value: undefined}); assert.equal(calls, stop + 1);
    }
    for (const request of ['return', 'throw']) {
      let calls = 0;
      const fresh = get(() => { ++calls; });
      const error = {}, value = {};
      if (request === 'throw') await assert.rejects(fresh.throw(error), actual => actual === error);
      else {
        let assimilations = 0;
        const result = fresh.return({then(resolve) { ++assimilations; resolve(value); }});
        assert.deepEqual(await result, {done: true, value}); assert.equal(assimilations, 1);
      }
      assert.deepEqual(await fresh.next(), {done: true, value: undefined}); assert.equal(calls, 0);
    }
    // Each captured environment remains distinct even after a sibling mutation.
    const own = {};
    set(own); let seen;
    await get(value => { seen = value; }).next(); assert.equal(seen, own);
    if (index === 0) {
      let other;
      await saved[1].get(value => { other = value; }).next(); assert.equal(other, cells[1]);
    }
  }
}
