import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// Independent native syntax oracle. Global reads remain effectful at each
// property, including binding replacement, missing names and property throws.
let scenarios = 0;
for (const [tool, option, direct, caught] of [
  [process.argv[2], '--source', false, false],
  [process.argv[2], '--buffer-source', false, false],
  [process.argv[2], '--direct-source', true, false],
  [process.argv[3], '--global-source', false, true]
]) {
  const source = execFileSync(tool, [option], {encoding: 'utf8'});
  const context = vm.createContext({});
  const recovered = vm.runInContext(source, context);
  const expression = direct ? 'Lookup' : 'Lookup.value';
  const native = caught ? `(function () { try { return {
    ["__proto__"]: {["__proto__"]: ${expression}}, ["__proto__"]: {["__proto__"]: ${expression}}
  }; } catch (error) { return error; } })` :
    `(function () { return {field0: {value: ${expression}}, field1: {value: ${expression}}}; })`;
  const reference = vm.runInContext(native, context);
  const prototype = vm.runInContext('Object.prototype', context);
  function observe(fn, token, fail, replace, missing, empty) {
    const events = [], failure = {}, objects = [], containers = [];
    let step = 0, reads = 0;
    const event = label => { events.push(label); if (++step === fail) throw failure; };
    delete context.Lookup;
    const container = id => {
      const value = {get value() { assert.equal(this, value); event(`property:${id}`);
        if (replace) current = container(id + 1);
        return token; }};
      containers.push(value); return value;
    };
    let current = container(1);
    if (!missing) Object.defineProperty(context, 'Lookup', {configurable: true, get() {
      event(`global:${++reads}`);
      const value = current;
      if (replace && direct) current = container(reads + 1);
      return empty ? null : value;
    }});
    const inspect = (object, key) => {
      assert.equal(Object.getPrototypeOf(object), prototype);
      const descriptor = Object.getOwnPropertyDescriptor(object, key);
      assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
      return descriptor.value;
    };
    try {
      const result = fn();
      if (result === failure) { assert.ok(caught); return {events, completion: 'caught injected'}; }
      if (result instanceof vm.runInContext('Error', context)) {
        assert.ok(caught); return {events, completion: `caught ${result.name}`};
      }
      const keys = caught ? ['__proto__'] : ['field0', 'field1'];
      assert.deepEqual(Reflect.ownKeys(result), keys);
      for (const [index, key] of keys.entries()) {
        const inner = inspect(result, key); objects.push(inner);
        assert.deepEqual(Reflect.ownKeys(inner), [caught ? '__proto__' : 'value']);
        assert.equal(inspect(inner, caught ? '__proto__' : 'value'), direct ?
          empty ? null : containers[replace ? index : 0] : token);
      }
      if (objects.length === 2) assert.notEqual(objects[0], objects[1]);
      return {events, completion: 'returned'};
    } catch (error) {
      if (error === failure) return {events, completion: 'threw injected'};
      if (error.name === 'TypeError' || error.name === 'ReferenceError') return {events, completion: `threw ${error.name}`};
      throw error;
    }
  }
  for (const token of [undefined, null, false, -0, NaN, 1n, Symbol('value'), {}])
    for (const replace of [false, true]) for (const missing of [false, true]) for (const empty of [false, true])
      for (let fail = 0; fail <= 4; ++fail) {
        assert.deepEqual(observe(recovered, token, fail, replace, missing, empty),
          observe(reference, token, fail, replace, missing, empty), `${option}/${fail}/${replace}/${missing}/${empty}`);
        ++scenarios;
      }
}
console.log(`Literal globals: ${scenarios} name/property effect, exception and identity scenarios passed`);
