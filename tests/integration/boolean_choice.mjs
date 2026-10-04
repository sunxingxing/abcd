import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// Handwritten language oracle. The fixture exposes AST grammar/observation;
// it does not certify arbitrary decoded arithmetic or modify VM admission.
function reference(form, context, depth) {
  return (a,b) => {
    let value;
    switch (context) {
      case 8: value = !!a.value ?? !!b.value; break;
      case 9: value = !!a.value && !!b.value; break;
      case 10: value = !!a.value || !!b.value; break;
      default: value = a.value;
    }
    for (let i = 0; i < depth; ++i) {
      switch (form) {
        case 0: value = value ? true : false; break;
        case 1: value = value ? false : true; break;
        case 2: value = value ? true : true; break;
        case 3: value = value ? false : false; break;
      }
    }
    switch (context) {
      case 0: case 8: case 9: case 10: return value;
      case 1: if (value) return true; else return false;
      case 2: return value || b.value;
      case 3: return value ?? b.value;
      case 4: return !value;
      case 5: return value + b.value;
      case 6: return value ** b.value;
      case 7: return value === b.value;
    }
  };
}
function observe(fn, left, right, mutate, failAt) {
  const trace = [], failure = Symbol('failure'); let steps = 0;
  const event = (...parts) => {trace.push(parts); if (++steps === failAt) throw failure;};
  const converting = {[Symbol.toPrimitive](hint) {event('coerce', hint); return 3;}};
  const forbidden = {[Symbol.toPrimitive]() {assert.fail('Boolean choice coerced condition');}};
  const values = [undefined, null, false, true, 0, -0, NaN, Infinity, '', 'text', 0n, 3n,
    Symbol('value'), converting, forbidden, Object.create(null)];
  const state = [values[left], values[right]];
  const args = state.map((_, index) => {
    let receiver;
    const target = Object.defineProperty({}, 'value', {get() {
      assert.equal(this, receiver); event('getter', index);
      if (mutate && index === 0) state[1] = undefined;
      return state[index];
    }});
    receiver = new Proxy(target, {get(target, key, owner) {
      assert.equal(key, 'value'); event('get', index); return Reflect.get(target, key, owner);
    }});
    return receiver;
  });
  try {
    const value = fn(...args);
    // Each execution has fresh payloads. Check identity against that run's
    // original inputs before comparing the corresponding cross-run identity.
    if (typeof value === 'symbol' || (typeof value === 'object' && value !== null)) {
      const identity = values.indexOf(value);
      assert.ok(identity >= 12, 'returned an unowned value');
      return {trace, inputIdentity: identity};
    }
    return {trace, value};
  }
  catch (error) {
    if (error === failure) return {trace, threw: 'injected'};
    assert.equal(error.name, 'TypeError'); return {trace, threw: 'TypeError'};
  }
}
let comparisons = 0;
function check(index, depth, exhaustive) {
  const context = Math.floor(index / 4), form = index % 4;
  const source = execFileSync(process.argv[2], ['--boolean-choice', String(index), String(depth)], {encoding:'utf8'});
  assert.ok(!source.includes(' ? '));
  const generated = vm.runInNewContext(source), expected = reference(form, context, depth);
  if (index === 0 && depth === 1) assert.match(source, /return !!a\.value;/);
  if (index === 1 && depth === 1) assert.match(source, /return !a\.value;/);
  if (index === 4 && depth === 1) assert.match(source, /if \(a\.value\)/);
  if (index === 5 && depth === 1) assert.match(source, /if \(!a\.value\)/);
  if (index === 24 && depth === 1) assert.match(source, /\(!!a\.value\) \*\* b\.value/);
  if (context === 0 && depth === 1) {
    const mutant = form === 0 ? source.replace('!!a.value', 'a.value') :
      form === 1 ? source.replace('!a.value', 'a.value') :
      source.replace(form === 2 ? '(a.value, true)' : '(a.value, false)', form === 2 ? 'true' : 'false');
    assert.notEqual(mutant, source);
    const broken = vm.runInNewContext(mutant);
    if (form < 2) assert.notDeepEqual(observe(broken, 15, 0, false, 0),
      observe(expected, 15, 0, false, 0), 'mutant preserved exact Boolean');
    else assert.notDeepEqual(observe(broken, 15, 0, false, 1),
      observe(expected, 15, 0, false, 1), 'mutant preserved condition retrieval');
  }
  for (let left = 0; left < 16; ++left) for (let right = 0; right < (exhaustive ? 16 : 1); ++right)
    for (const mutate of [false,true]) for (let failAt = 0; failAt < 10; ++failAt) {
      // Addition/exponentiation legitimately coerce the RHS. Never classify
      // an assertion from a forbidden Boolean hook as an expected TypeError.
      const b = (context === 5 || context === 6) && right === 14 ? 13 : right;
      assert.deepEqual(observe(generated, left, b, mutate, failAt),
        observe(expected, left, b, mutate, failAt), `choice ${index}, depth ${depth}`);
      ++comparisons;
    }
}
for (let index = 0; index < 44; ++index) check(index, 1, true);
for (const context of [0,1,4,6,8,9,10]) for (let form = 0; form < 4; ++form)
  for (const depth of [2,200,201,402,804]) check(context * 4 + form, depth, false);
console.log(`${comparisons} Boolean-choice exact-value/predicate/grammar/getter/Proxy/throw comparisons passed`);
