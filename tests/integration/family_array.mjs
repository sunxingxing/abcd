import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
const normal = execFileSync(process.argv[2], ['--source'], {encoding:'utf8'}).trim().split('\n').map(JSON.parse);
const phased = execFileSync(process.argv[3], ['--buffered-array-json'], {encoding:'utf8'}).trim().split('\n').map(JSON.parse);
assert.equal(normal.length, 3); assert.equal(phased.length, 3);
let scenarios = 0;
function pair(source, phase) {
  assert.ok(!source.includes('INCOMPLETE'));
  const reference = phase ? `class { run(callback) {
    let cell = callback; const array = ['run', 42];
    try { callback(array); } catch (error) { cell = error; return cell; }
    return cell;
  } }` : `function(callback) { const array = ['run',42,null,true,-0]; callback(array); return array; }`;
  return vm.runInNewContext(`(() => {
    const prototype = Array.prototype;
    Object.defineProperty(prototype, '0', {set() { throw new Error('inherited setter'); }});
    for (const name of ['call','apply','bind']) Function.prototype[name] = function() { throw new Error('ambient adapter'); };
    prototype[Symbol.iterator] = function() { throw new Error('unnecessary iterator'); };
    globalThis.Array = function() { throw new Error('mutable Array constructor'); };
    return {recovered: ${phase ? `(class { ${source} }).prototype.run` : `(${source})`},
      reference: ${phase ? `(${reference}).prototype.run` : `(${reference})`}, prototype};
  })()`);
}
function exercise(fn, realm, phase, mode, thrown, proxy) {
  assert.equal(fn.length, 1);
  const trace = [], arrays = [], marker = {};
  const callback = function(array) {
    assert.equal(this, undefined); assert.equal(arguments.length, 1);
    assert.ok(Array.isArray(array)); assert.equal(Object.getPrototypeOf(array), realm.prototype);
    const expected = phase ? ['run',42] : ['run',42,null,true,-0];
    assert.deepEqual(Object.keys(array), expected.map((_,i) => String(i)));
    assert.equal(array.length, expected.length);
    for (let i = 0; i < expected.length; ++i) {
      const descriptor = Object.getOwnPropertyDescriptor(array, String(i));
      assert.equal(descriptor.writable, true); assert.equal(descriptor.enumerable, true);
      assert.equal(descriptor.configurable, true); assert.ok(Object.is(descriptor.value, expected[i]));
    }
    arrays.push(array); trace.push('call');
    if (mode === 'throw') throw thrown;
    if (mode === 'mutate') { array[0] = marker; array.length = 1; }
    return marker;
  };
  for (const key of ['call','apply','bind']) Object.defineProperty(callback, key, {get() { throw new Error('callee adapter'); }});
  const callable = proxy ? new Proxy(callback, {apply(target, receiver, args) {
    assert.equal(receiver, undefined); trace.push('proxy');
    if (mode === 'proxy-throw') throw thrown;
    return Reflect.apply(target, receiver, args);
  }}) : callback;
  for (let i = 0; i < 2; ++i) {
    const fails = mode === 'throw' || (proxy && mode === 'proxy-throw');
    if (!phase && fails) {
      let raised = false;
      try { Reflect.apply(fn, {wrongReceiver:true}, [callable]); }
      catch (error) { raised = true; assert.ok(Object.is(error, thrown)); }
      assert.equal(raised, true); trace.push('throw');
    } else {
      const actual = Reflect.apply(fn, {wrongReceiver:true}, [callable]);
      assert.ok(Object.is(actual, phase ? fails ? thrown : callable : arrays.at(-1)));
      if (!phase && mode === 'mutate') assert.equal(actual[0], marker);
      trace.push('return');
    }
  }
  if (arrays.length > 1) assert.notEqual(arrays[0], arrays[1], 'fresh allocation per invocation');
  return trace;
}
for (const [phase, reports] of [[false,normal],[true,phased]]) for (const {source} of reports) {
  const realm = pair(source, phase);
  for (const mode of ['return','mutate','throw','proxy-throw'])
    for (const thrown of [undefined,null,false,-0,NaN,1n,Symbol(),{},()=>{}])
      for (const proxy of [false,true]) {
        assert.deepEqual(exercise(realm.recovered,realm,phase,mode,thrown,proxy),
          exercise(realm.reference,realm,phase,mode,thrown,proxy)); ++scenarios;
      }
  for (const invalid of [undefined,null,false,0,1n,Symbol(),{}]) {
    for (const fn of [realm.recovered,realm.reference]) {
      if (phase) assert.equal(Reflect.apply(fn,{},[invalid]).name, 'TypeError');
      else assert.throws(() => Reflect.apply(fn,{},[invalid]), error => error.name === 'TypeError');
    }
    ++scenarios;
  }
  // Primitive-value and array/object mutations remain syntactically valid.
  const changed = source.replace('42', '43'); assert.notEqual(changed,source);
  const corrupted = pair(changed,phase);
  assert.throws(() => exercise(corrupted.recovered,corrupted,phase,'return',null,false));
  const object = source.replace(/\["run", 42(, null, true, \(-0\))?\]/,
    phase ? '({0:"run",1:42})' : '({0:"run",1:42,2:null,3:true,4:-0})');
  assert.notEqual(object, source);
  const identity = pair(object, phase);
  assert.throws(() => exercise(identity.recovered,identity,phase,'return',null,false));

}
console.log(`Primitive array family: ${scenarios} allocation/descriptor/receiver/exception/cell scenarios passed.`);
