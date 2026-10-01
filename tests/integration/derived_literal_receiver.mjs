import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// The oracle is an independently authored complete constructor. The engine
// receives serialized ABC only; neither this body nor mock state enters it.
const reference = `constructor(argument) {
  super(argument);
  this.callback = {callback: (0, () => this), argument: this.argument};
}`;
const reports = execFileSync(process.argv[2], ['--receiver-literal-source'], {encoding: 'utf8'})
  .trim().split('\n').map(JSON.parse);
assert.equal(reports.length, 3);
let scenarios = 0;
function exercise(body, baseKind, getterKind, setterKind, proxy, value) {
  const trace = [], tags = new WeakMap(), callbacks = new WeakMap();
  const raised = Symbol('raised'), token = {};
  tags.set(token, 'token');
  const normalize = value => {
    if (typeof value === 'function') return ['function', value.name, value.length];
    if (value && typeof value === 'object') return tags.get(value) ?? 'input';
    return value;
  };
  const event = (name, ...values) => trace.push([name, ...values.map(normalize)]);
  let Type, expected, reads = 0, stored;
  const property = {
    get() {
      assert.equal(this, expected); event('argument:get', this, ++reads);
      if (getterKind === 'throw') throw raised;
      if (getterKind === 'mutate') stored = token;
      return stored;
    }, configurable: true
  };
  const replacement = new Proxy({}, {
    get(target, key, receiver) { event('replacement:get', key, receiver); return Reflect.get(target, key, receiver); },
    set(target, key, value, receiver) {
      assert.equal(receiver, replacement); event('replacement:set', key);
      return Reflect.set(target, key, value, receiver);
    }
  });
  tags.set(replacement, 'replacement');
  Object.defineProperty(replacement, 'argument', property);
  function Base(argument) {
    assert.equal(new.target, Type); event('base', argument);
    expected = this; tags.set(this, 'receiver'); stored = argument;
    Object.defineProperty(this, 'argument', property);
    if (baseKind === 'throw') throw raised;
    if (baseKind === 'object') { expected = replacement; return replacement; }
    return baseKind === 'primitive' ? 17 : baseKind === 'null' ? null : undefined;
  }
  if (setterKind !== 'none') Object.defineProperty(Base.prototype, 'callback', {
    set(member) {
      assert.equal(this, expected); event('callback:set', this);
      // The setter can invoke the freshly installed closure before construction
      // returns. It must already see the same initialized receiver cell.
      assert.equal(Reflect.apply(member.callback, {}, []), this);
      callbacks.set(this, member);
      if (setterKind === 'throw') throw raised;
    }, configurable: true
  });
  const Parent = proxy ? new Proxy(Base, {construct(target, args, actual) {
    assert.equal(actual, Type); event('construct', ...args);
    return Reflect.construct(target, args, actual);
  }}) : Base;
  const context = vm.createContext({Parent, raised});
  const prototype = vm.runInContext('Object.prototype', context);
  vm.runInContext(`
    Object.defineProperty(Object.prototype, 'callback', {set() { throw raised; }, configurable: true});
    Object.defineProperty(Object.prototype, 'argument', {set() { throw raised; }, configurable: true});
    Function.prototype.call = Function.prototype.apply = Function.prototype.bind = () => { throw raised; };
  `, context);
  Type = vm.runInContext(`(class Type extends Parent { ${body} })`, context);
  assert.equal(Type.length, 1);
  const identities = [];
  for (let invocation = 0; invocation < 2; ++invocation) {
    reads = 0;
    let instance;
    try { instance = new Type(value); }
    catch (error) { assert.equal(error, raised); event('throw'); continue; }
    assert.equal(instance, expected);
    const member = callbacks.get(instance) ?? instance.callback;
    assert.equal(Object.getPrototypeOf(member), prototype);
    assert.deepEqual(Reflect.ownKeys(member), ['callback', 'argument']);
    assert.equal(reads, 1, 'initializer getter executes exactly once');
    for (const key of ['callback', 'argument']) {
      const descriptor = Object.getOwnPropertyDescriptor(member, key);
      assert.equal(descriptor.writable, true); assert.equal(descriptor.enumerable, true);
      assert.equal(descriptor.configurable, true); assert.ok('value' in descriptor);
    }
    event('literal:argument', member.argument);
    assert.equal(member.argument, getterKind === 'mutate' ? token : value);
    assert.equal(member.callback.name, ''); assert.equal(member.callback.length, 0);
    for (const receiver of [undefined, null, {}, value])
      assert.equal(Reflect.apply(member.callback, receiver, []), instance);
    stored = 'later';
    assert.equal(member.argument, getterKind === 'mutate' ? token : value);
    assert.equal(Reflect.apply(member.callback, {}, []), instance);
    assert.ok(!identities.includes(member)); identities.push(member);
    event('return', instance, Reflect.ownKeys(instance).join(','));
  }
  return trace;
}
for (const {source} of reports) {
  assert.ok(!source.includes('INCOMPLETE'));
  for (const base of ['ordinary', 'object', 'primitive', 'null', 'throw'])
    for (const getter of ['value', 'mutate', 'throw']) for (const setter of ['none', 'save', 'throw'])
      for (const proxy of [false, true]) for (const value of [undefined, null, -0, 1n, Symbol(), {}, () => {}]) {
        assert.deepEqual(exercise(source, base, getter, setter, proxy, value),
          exercise(reference, base, getter, setter, proxy, value)); ++scenarios;
      }
  const occurrence = source.match(/\$value\d+\.argument/);
  assert.ok(occurrence);
  for (const replacement of [`(${occurrence[0]}, ${occurrence[0]})`, 'null']) {
    const mutated = source.replace(occurrence[0], replacement);
    assert.notEqual(mutated, source);
    assert.doesNotThrow(() => new vm.Script(`(class Type extends Parent { ${mutated} })`));
    assert.throws(() => exercise(mutated, 'ordinary', 'value', 'none', false, {}));
  }
}
console.log(`Derived receiver literal: ${scenarios} complete constructor/getter/capture/parent-result/Proxy/throw scenarios passed.`);
