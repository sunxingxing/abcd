import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const reports = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8'}).trim().split('\n').map(JSON.parse);
assert.equal(reports.length, 24);
let scenarios = 0;
function exercise(source, count, values, mode, proxy) {
  const trace = [], raised = Symbol('arbitrary thrown value'), state = [...values];
  let object;
  const target = {};
  for (let i = 0; i < count; ++i) Object.defineProperty(target, `field${i}`, {get() {
    assert.equal(this, object); trace.push(['field', i]);
    if (mode === `throw${i}`) throw raised;
    if (mode === 'change-next' && i + 1 < count) state[i + 1] = undefined;
    return state[i];
  }});
  function action() {
    assert.equal(this, object); assert.equal(arguments.length, 0); trace.push(['call']);
    if (mode === 'call-throw') throw raised;
  }
  for (const name of ['call', 'apply', 'bind']) Object.defineProperty(action, name, {get() {
    throw new Error('mutable call helper used');
  }});
  Object.defineProperty(target, 'action', {get() {
    assert.equal(this, object); trace.push(['callee']);
    if (mode === 'callee-throw') throw raised;
    return mode === 'non-callable' ? null : action;
  }});
  Object.defineProperty(target, 'after', {get() {
    assert.equal(this, object); trace.push(['after:get']);
    if (mode === 'after-get-throw') throw raised;
    return function() {
      assert.equal(this, object); trace.push(['after:call']);
      if (mode === 'after-call-throw') throw raised;
    };
  }});
  object = proxy ? new Proxy(target, {get(actual, key, receiver) {
    assert.equal(receiver, object); trace.push(['proxy', key]);
    return Reflect.get(actual, key, receiver);
  }}) : target;
  const realm = vm.createContext({});
  vm.runInContext(`for (const name of ['call', 'apply', 'bind']) {
    Function.prototype[name] = function() { throw new Error('ambient helper used'); };
  }`, realm);
  const fn = vm.runInContext(`(${source})`, realm);
  assert.equal(fn.length, 1);
  try { assert.equal(fn(object), undefined); trace.push(['returned']); }
  catch (error) {
    if (mode === 'non-callable') { assert.equal(error.name, 'TypeError'); trace.push(['type-error']); }
    else { assert.equal(error, raised); trace.push(['throw']); }
  }
  return trace;
}
const variants = [undefined, null, false, 0, -0, NaN, '', Symbol('value'), {
  [Symbol.toPrimitive]() { throw new Error('strict comparison coerced an object'); }
}];
for (const {source, count, positive, abrupt} of reports) {
  assert.ok(!source.includes('INCOMPLETE'));
  assert.ok(source.includes(' || '));
  const conditions = Array.from({length: count}, (_, i) =>
    `object.field${i} ${i + 1 === count && positive ? '===' : '!=='} undefined`);
  const reference = `function(object) { if (${conditions.join(' || ')}) { object.action(); ${abrupt ? 'return;' : ''} } object.after(); }`;
  const modes = ['return', 'callee-throw', 'call-throw', 'non-callable', 'change-next', 'after-get-throw', 'after-call-throw',
    ...Array.from({length: count}, (_, i) => `throw${i}`)];
  const combinations = variants.length ** count;
  for (let code = 0; code < combinations; ++code) {
    let remaining = code;
    const values = Array.from({length: count}, () => {
      const value = variants[remaining % variants.length]; remaining = Math.floor(remaining / variants.length); return value;
    });
    for (const mode of modes) for (const proxy of [false, true]) {
      assert.deepEqual(exercise(source, count, values, mode, proxy), exercise(reference, count, values, mode, proxy));
      ++scenarios;
    }
  }
  const changed = source.replace(' || ', ' && ');
  assert.notEqual(changed, source);
  assert.doesNotThrow(() => new vm.Script(`(${changed})`));
  assert.throws(() => assert.deepEqual(exercise(changed, count, Array(count).fill(null), 'return', false),
    exercise(reference, count, Array(count).fill(null), 'return', false)));
  const repeated = source.replace('object.field0', '(object.field0, object.field0)');
  assert.notEqual(repeated, source);
  assert.doesNotThrow(() => new vm.Script(`(${repeated})`));
  assert.throws(() => assert.deepEqual(exercise(repeated, count, Array(count).fill(null), 'return', false),
    exercise(reference, count, Array(count).fill(null), 'return', false)));
}
console.log(`Shared conditions: ${scenarios} complete short-circuit/getter/call/receiver/throw scenarios passed.`);
