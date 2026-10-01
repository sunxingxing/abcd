import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// Serialized ABC family recovery completes before the independent reference is
// evaluated. The fixture has a primitive buffer and no child function bindings.
const reports = execFileSync(process.argv[2], ['--buffered-lexical-json'], {encoding: 'utf8'}).trim().split('\n').map(JSON.parse);
assert.equal(reports.length, 3);
let scenarios = 0;
for (const {source} of reports) {
  assert.ok(!source.includes('INCOMPLETE'));
  const pair = vm.runInNewContext(`(() => {
    Object.defineProperty(Object.prototype, 'run', {set() { throw new Error('inherited setter'); }});
    return {
      recovered: (class { ${source} }).prototype.run,
      reference: (class { run(callback) {
        let cell = callback; const object = {run: 42};
        try { callback(object); } catch (error) { cell = error; return cell; }
        return cell;
      } }).prototype.run,
      prototype: Object.prototype
    };
  })()`);
  assert.equal(pair.recovered.name, 'run'); assert.equal(pair.recovered.length, 1);
  function exercise(fn, mode, thrown, proxy) {
    const trace = [], objects = [], result = {};
    const callback = function(object) {
      assert.equal(this, undefined); assert.equal(arguments.length, 1);
      assert.equal(Object.getPrototypeOf(object), pair.prototype);
      assert.deepEqual(Object.keys(object), ['run']);
      assert.deepEqual(Object.getOwnPropertyDescriptor(object, 'run'), {
        value: 42, writable: true, enumerable: true, configurable: true
      });
      objects.push(object); trace.push('call');
      if (mode === 'throw') throw thrown;
      if (mode === 'mutate') { object.run = 99; object.extra = result; }
      return result;
    };
    for (const key of ['call', 'apply', 'bind']) Object.defineProperty(callback, key, {
      get() { throw new Error('ambient invocation adapter'); }
    });
    const callable = proxy ? new Proxy(callback, {apply(target, receiver, args) {
      assert.equal(receiver, undefined); trace.push('proxy');
      if (mode === 'proxy-throw') throw thrown;
      return Reflect.apply(target, receiver, args);
    }}) : callback;
    for (let i = 0; i < 2; ++i) {
      const actual = Reflect.apply(fn, {wrongReceiver: true}, [callable]);
      assert.equal(actual, mode === 'throw' || (proxy && mode === 'proxy-throw') ? thrown : callable);
      trace.push('return');
    }
    if (objects.length) assert.notEqual(objects[0], objects[1], 'fresh buffer allocation per invocation');
    return trace;
  }
  for (const mode of ['return', 'mutate', 'throw', 'proxy-throw'])
    for (const thrown of [undefined, null, false, -0, NaN, 1n, Symbol(), {}, () => {}])
      for (const proxy of [false, true]) {
        assert.deepEqual(exercise(pair.recovered, mode, thrown, proxy), exercise(pair.reference, mode, thrown, proxy));
        ++scenarios;
      }
  for (const invalid of [undefined, null, false, 0, 1n, Symbol(), {}]) {
    for (const fn of [pair.recovered, pair.reference]) {
      const error = Reflect.apply(fn, {}, [invalid]);
      assert.equal(error.name, 'TypeError');
    }
    ++scenarios;
  }
}
console.log(`Phased primitive buffer/cell family: ${scenarios} receiver/identity/exception scenarios passed.`);
