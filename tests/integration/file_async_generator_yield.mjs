import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const preservation=process.argv[3]==='--preserved';
const fixtures = execFileSync(process.argv[2], [preservation?'--preserved-async-yield-json':'--async-generator-yield-json'],
  {encoding: 'utf8', timeout: 60_000, maxBuffer: 16_000_000}).trim().split('\n').map(JSON.parse);
assert.equal(fixtures.length, preservation?24:159);
assert.equal(fixtures.filter(f => f.rejected).length, preservation?0:39);
const deferred = () => {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return {promise, resolve, reject};
};
const discarded = {get then() { assert.fail('discarded callback and NEXT values must not be awaited'); }};
const rejectAs = (promise, reason) => promise.then(() => assert.fail('request must reject'), actual => {
  assert.equal(actual, reason); // Do not return/assimilate a rejection reason.
});
for (const fixture of fixtures.filter(f => !f.rejected)) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  assert.ok(fixture.source.includes('async function*'));
  assert.equal((fixture.source.match(/\(yield /g) ?? []).length, fixture.count);
  assert.equal((fixture.source.match(/\(await /g) ?? []).length, fixture.awaited ? fixture.count : 0);
  const {capture} = await import(`data:text/javascript;base64,${Buffer.from(`export const capture = ${fixture.source};`).toString('base64')}`);
  let entered = false, get, set;
  capture(() => !entered && (entered = true), () => { if (fixture.catch_parent) throw {}; return {}; },
    (child, writer) => { get = child; set = writer; });
  const perIteration = fixture.awaited ? 3 : 2;
  const waitCount = fixture.count * (fixture.awaited ? 2 : 1);
  const readiness = Array.from({length: waitCount}, deferred), gates = [], observed = [], values = [];
  let thenGets = 0, thenCalls = 0, calls = 0, waitIndex = 0;
  const generator = get(function(value) {
    assert.equal(this, undefined); assert.equal(arguments.length, 1);
    const stage = calls++ % perIteration;
    observed.push(value);
    if (stage === perIteration - 1) { assert.equal(value, discarded); return discarded; }
    const index = waitIndex++;
    const result = {get then() {
      ++thenGets;
      return function(resolve, reject) {
        assert.equal(this, result); assert.equal(arguments.length, 2); ++thenCalls;
        gates[index] = {resolve, reject}; readiness[index].resolve();
      };
    }};
    return result;
  });
  let cell = {}; set(cell); assert.equal(calls, 0);
  for (let iteration = 0; iteration < fixture.count; ++iteration) {
    const request = generator.next(discarded);
    for (let stage = 0; stage < (fixture.awaited ? 2 : 1); ++stage) {
      const index = iteration * (fixture.awaited ? 2 : 1) + stage;
      await readiness[index].promise;
      assert.equal(observed.at(-1), cell);
      assert.equal(thenGets, index + 1); assert.equal(thenCalls, index + 1);
      cell = {}; set(cell); const value = {}; values.push(value); gates[index].resolve(value);
    }
    const result = await request; assert.equal(result.done, false); assert.equal(result.value, values.at(-1));
  }
  assert.deepEqual(await generator.next(discarded), {done: true, value: undefined});
  assert.equal(calls, perIteration * fixture.count); assert.equal(waitIndex, waitCount);
  assert.equal(thenGets, waitCount); assert.equal(thenCalls, waitCount);

  // A queued RETURN cannot interrupt either source wait or implicit yield wait.
  // Its own await delays subsequent requests and suppresses the NEXT callback.
  const ready = deferred(), returnReady = deferred(), outgoing = {}, payload = {};
  let releaseYield, releaseReturn, returnGets = 0, callbackCalls = 0;
  const queued = get(function() {
    const stage = callbackCalls++;
    if (fixture.awaited && stage === 0) return undefined;
    assert.equal(stage, fixture.awaited ? 1 : 0);
    return {then(resolve) { releaseYield = resolve; ready.resolve(); }};
  });
  const first = queued.next(); await ready.promise;
  const returnInput = {get then() { ++returnGets; return function(resolve) { releaseReturn = resolve; returnReady.resolve(); }; }};
  let nextSettled = false, throwSettled = false;
  const thrown = {get then() { assert.fail('THROW reason must retain identity'); }};
  const returned = queued.return(returnInput);
  const next = queued.next().then(result => { nextSettled = true; return result; });
  const throwing = queued.throw(thrown).then(() => assert.fail('queued throw must reject'), reason => {
    throwSettled = true; assert.equal(reason, thrown);
  });
  await Promise.resolve(); assert.equal(returnGets, 0); assert.equal(nextSettled, false); assert.equal(throwSettled, false);
  releaseYield(outgoing); assert.deepEqual(await first, {done: false, value: outgoing});
  await returnReady.promise; assert.equal(returnGets, 1); assert.equal(nextSettled, false); assert.equal(throwSettled, false);
  releaseReturn(payload); assert.deepEqual(await returned, {done: true, value: payload});
  assert.deepEqual(await next, {done: true, value: undefined}); await throwing;
  assert.equal(callbackCalls, fixture.awaited ? 2 : 1);

  // Outbound await failures and injected THROW/RETURN rejection select the
  // original source catch. It may replace the exception without assimilating
  // either exception object or its discarded callback result.
  for (const site of ['call', 'then-get', 'then-call', 'throw', 'return']) for (const override of [false, true]) {
    const reason = {get then() { assert.fail('error reason must not be awaited'); }}, replacement = {};
    let caught = 0, calls = 0;
    const failed = get(function(value) {
      assert.equal(this, undefined); assert.equal(arguments.length, 1);
      if (value === reason) {
        assert.equal(fixture.caught, true); ++caught;
        if (override) throw replacement;
        return discarded;
      }
      const stage = calls++;
      if (fixture.awaited && stage === 0) return undefined;
      if (site === 'call') throw reason;
      if (site === 'then-get' || site === 'then-call') return {get then() {
        if (site === 'then-get') throw reason;
        return function() { throw reason; };
      }};
      return {};
    });
    let request = failed.next();
    if (site === 'throw' || site === 'return') {
      assert.equal((await request).done, false);
      request = site === 'throw' ? failed.throw(reason) : failed.return({then(resolve, reject) { reject(reason); }});
    }
    const queuedNext = failed.next();
    if (fixture.caught && !override) assert.deepEqual(await request, {done: true, value: undefined});
    else await rejectAs(request, fixture.caught ? replacement : reason);
    assert.deepEqual(await queuedNext, {done: true, value: undefined});
    assert.equal(caught, fixture.caught ? 1 : 0);
    assert.equal(calls, fixture.awaited ? 2 : 1);
  }
  if (fixture.awaited) {
    const reason = {}; let caught = 0, calls = 0;
    const failed = get(value => {
      if (value === reason) { ++caught; return discarded; }
      ++calls; return Promise.reject(reason);
    });
    if (fixture.caught) assert.deepEqual(await failed.next(), {done: true, value: undefined});
    else await rejectAs(failed.next(), reason);
    assert.equal(calls, 1); assert.equal(caught, fixture.caught ? 1 : 0);
  }
  let lazyCalls = 0;
  const unstarted = get(() => { ++lazyCalls; });
  assert.deepEqual(await unstarted.return(Promise.resolve(payload)), {done: true, value: payload});
  assert.equal(lazyCalls, 0);
  await rejectAs(get(() => { ++lazyCalls; }).throw(thrown), thrown); assert.equal(lazyCalls, 0);
}

if(preservation)console.log(JSON.stringify({preservedAsyncGeneratorFamilies:fixtures.length}));
