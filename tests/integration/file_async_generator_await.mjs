import assert from 'node:assert/strict';
import {sourceAwaitSites} from '../tools/grouping_tokens.mjs';
import {execFileSync} from 'node:child_process';

const fixtures = execFileSync(process.argv[2], ['--async-generator-await-json'],
  {encoding: 'utf8', timeout: 60_000, maxBuffer: 16_000_000}).trim().split('\n').map(JSON.parse);
assert.equal(fixtures.length, 84);
assert.equal(fixtures.filter(fixture => fixture.rejected).length, 18);
const deferred = () => {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return {promise, resolve, reject};
};
for (const fixture of fixtures.filter(fixture => !fixture.rejected)) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  assert.ok(fixture.source.includes('async function*'));
  assert.equal(sourceAwaitSites(fixture.source).length, fixture.count);
  const {capture} = await import(`data:text/javascript;base64,${Buffer.from(`export const capture = ${fixture.source};`).toString('base64')}`);
  let entered = false, get, set;
  capture(() => !entered && (entered = true), () => { if (fixture.catch_parent) throw {}; return {}; },
    (child, writer) => { get = child; set = writer; });

  const values = Array.from({length: fixture.count + 1}, () => ({}));
  const readiness = Array.from({length: fixture.count}, deferred), gates = [], observed = [];
  let thenGets = 0, thenCalls = 0;
  const generator = get(function(value) {
    assert.equal(this, undefined); assert.equal(arguments.length, 1);
    const index = observed.length; observed.push(value);
    const result = {get then() {
      ++thenGets;
      return function(resolve, reject) {
        assert.equal(this, result); assert.equal(arguments.length, 2); ++thenCalls;
        gates[index] = {resolve, reject}; readiness[index].resolve();
      };
    }};
    return result;
  });
  set(values[0]); assert.deepEqual(observed, []);
  const first = generator.next({get then() { assert.fail('initial next payload is ignored'); }});
  await readiness[0].promise;
  let returnReady = deferred(), releaseReturn, returnGets = 0;
  const payload = {}, error = {get then() { assert.fail('queued throw must preserve its reason'); }};
  const returnInput = {get then() { ++returnGets; return function(resolve) { releaseReturn = resolve; returnReady.resolve(); }; }};
  let returnSettled = false, nextSettled = false, throwSettled = false;
  const queuedReturn = generator.return(returnInput).then(result => { returnSettled = true; return result; });
  const queuedNext = generator.next().then(result => { nextSettled = true; return result; });
  const queuedThrow = generator.throw(error).then(() => assert.fail('queued throw must reject'), reason => {
    throwSettled = true; assert.equal(reason, error);
  });
  for (let index = 0; index < fixture.count; ++index) {
    await readiness[index].promise; await Promise.resolve();
    assert.equal(observed.length, index + 1); assert.equal(observed[index], values[index]);
    assert.equal(thenGets, index + 1); assert.equal(thenCalls, index + 1);
    assert.equal(returnGets, 0); assert.equal(returnSettled, false);
    assert.equal(nextSettled, false); assert.equal(throwSettled, false);
    set(values[index + 1]); gates[index].resolve(index);
  }
  assert.deepEqual(await first, {done: true, value: undefined});
  await returnReady.promise; await Promise.resolve();
  assert.equal(returnGets, 1); assert.equal(nextSettled, false); assert.equal(throwSettled, false);
  releaseReturn(payload);
  const returned = await queuedReturn; assert.equal(returned.done, true); assert.equal(returned.value, payload);
  assert.deepEqual(await queuedNext, {done: true, value: undefined}); await queuedThrow;
  assert.equal(observed.length, fixture.count);

  for (const stop of new Set([0, fixture.count - 1])) for (const override of [false, true]) {
    const cell = {}, reason = {get then() { assert.fail('rejection reason must not be assimilated'); }}, replacement = {};
    set(cell); let calls = 0, catchCalls = 0;
    const failed = get(function(value) {
      assert.equal(this, undefined);
      if (value === reason) {
        assert.equal(fixture.caught, true); ++catchCalls;
        if (override) throw replacement;
        return {get then() { assert.fail('catch callback result is discarded, without await'); }};
      }
      assert.equal(value, cell);
      return calls++ === stop ? Promise.reject(reason) : Promise.resolve(undefined);
    });
    const request = failed.next(), next = failed.next();
    if (fixture.caught && !override) assert.deepEqual(await request, {done: true, value: undefined});
    else await request.then(() => assert.fail('request must reject'), actual => {
      assert.equal(actual, fixture.caught ? replacement : reason);
    });
    assert.deepEqual(await next, {done: true, value: undefined});
    assert.equal(calls, stop + 1); assert.equal(catchCalls, fixture.caught ? 1 : 0);
  }
  for (const site of ['call', 'then-get', 'then-call']) {
    const reason = {}; let calls = 0, catchCalls = 0;
    const failed = get(function(value) {
      if (value === reason) { ++catchCalls; return undefined; }
      ++calls;
      if (site === 'call') throw reason;
      return {get then() {
        if (site === 'then-get') throw reason;
        return function() { throw reason; };
      }};
    });
    const request = failed.next();
    if (fixture.caught) assert.deepEqual(await request, {done: true, value: undefined});
    else await request.then(() => assert.fail('await failure must reject'), actual => { assert.equal(actual, reason); });
    assert.equal(calls, 1); assert.equal(catchCalls, fixture.caught ? 1 : 0);
  }
}
