import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const literal = process.argv[3] === '--literal';
const reports = execFileSync(process.argv[2], [literal ? '--literal-source' : '--source'], {encoding: 'utf8'})
  .trim().split('\n').map(JSON.parse);
assert.equal(reports.length, 3);
let scenarios = 0;
for (const {source} of reports) {
  assert.ok(!source.includes('INCOMPLETE'));
  function exercise(body, baseKind, setter, value, proxy) {
    const trace = [], tags = new WeakMap(), callbacks = new WeakMap();
    const raised = Symbol('same exception');
    let Type, receiver;
    const normalize = value => {
      if (typeof value === 'function') return ['function', value.name, value.length];
      if (value && typeof value === 'object') {
        const tag = tags.get(value); if (tag) return tag;
        const own = literal && Object.getOwnPropertyDescriptor(value, 'callback');
        if (own) return ['literal', Reflect.ownKeys(value).join(','),
          typeof own.value, own.value?.name, own.value?.length,
          own.writable, own.enumerable, own.configurable];
        return 'argument';
      }
      return value;
    };
    const event = (name, ...values) => trace.push([name, ...values.map(normalize)]);
    const replacement = new Proxy({}, {set(target, key, member, actual) {
      assert.equal(actual, replacement); event('replacement:set', key, member);
      return Reflect.set(target, key, member, actual);
    }});
    tags.set(replacement, 'replacement');
    function Base(argument) {
      assert.equal(new.target, Type); event('base', argument);
      receiver = this; tags.set(this, 'receiver'); this.argument = argument;
      if (baseKind === 'throw') throw raised;
      return baseKind === 'object' ? replacement : baseKind === 'primitive' ? 17 :
        baseKind === 'null' ? null : undefined;
    }
    if (setter !== 'none') Object.defineProperty(Base.prototype, 'callback', {
      set(callback) {
        assert.equal(this, receiver); event('callback:set', callback);
        if (setter === 'throw') throw raised;
        callbacks.set(this, callback);
      }, configurable: true
    });
    const parent = proxy ? new Proxy(Base, {construct(target, args, actual) {
      assert.equal(actual, Type); event('construct', ...args);
      return Reflect.construct(target, args, actual);
    }}) : Base;
    const context = vm.createContext({Parent: parent, raised});
    const prototype = vm.runInContext('Object.prototype', context);
    if (literal) vm.runInContext(`Object.defineProperty(Object.prototype, 'callback', {
      set() { throw raised; }, configurable: true
    });`, context);
    Type = vm.runInContext(`(class Type extends Parent { ${body} })`, context);
    assert.equal(Type.length, 1);
    try {
      const instance = new Type(value);
      assert.equal(instance, baseKind === 'object' ? replacement : receiver);
      const member = callbacks.get(instance) ?? instance.callback;
      if (literal) {
        assert.equal(Object.getPrototypeOf(member), prototype);
        assert.deepEqual(Reflect.ownKeys(member), ['callback']);
        const descriptor = Object.getOwnPropertyDescriptor(member, 'callback');
        assert.equal(descriptor.writable, true); assert.equal(descriptor.enumerable, true);
        assert.equal(descriptor.configurable, true);
      }
      const callback = literal ? member.callback : member;
      assert.equal(callback.name, ''); assert.equal(callback.length, 0);
      // Lexical receiver identity survives caller receiver changes and later
      // object changes. Returning a parent replacement is equally observable.
      for (const wrong of [undefined, null, {}, value])
        assert.equal(Reflect.apply(callback, wrong, []), instance);
      instance.argument = 'changed';
      assert.equal(Reflect.apply(callback, {}, []), instance);
      event('keys', Reflect.ownKeys(instance).join(',')); event('return', instance);
    } catch (error) {
      assert.equal(error, raised, 'unexpected constructor/capture failure'); event('throw');
    }
    return trace;
  }
  const reference = literal ?
    'constructor(argument) { super(argument); this.callback = {callback: (0, () => this)}; }' :
    'constructor(argument) { super(argument); this.callback = (() => this); }';
  for (const kind of ['ordinary', 'object', 'primitive', 'null', 'throw'])
    for (const setter of ['none', 'save', 'throw']) for (const proxy of [false, true])
      for (const value of [undefined, null, -0, 1n, Symbol(), {}, () => {}]) {
        assert.deepEqual(exercise(source, kind, setter, value, proxy),
          exercise(reference, kind, setter, value, proxy)); ++scenarios;
      }
  const changed = source.replace('($local3 = $value1);', '($local3 = null);');
  assert.notEqual(changed, source);
  assert.throws(() => exercise(changed, 'ordinary', 'none', {}, false));
  if (literal) {
    // Removing the explicit empty NamedEvaluation wrapper makes JS infer the
    // property key as the arrow's name. Behavior includes observable name.
    const inferred = source.replace('({[""]: () => {', '(() => {').replace('}})[""]}', '})}');
    assert.notEqual(inferred, source);
    assert.doesNotThrow(() => new vm.Script(`(class Type extends Parent { ${inferred} })`));
    assert.throws(() => exercise(inferred, 'ordinary', 'none', {}, false));
  }
}
console.log(`Derived ${literal ? 'literal closure' : 'cell'} constructors: ${scenarios} complete constructor/capture/parent-result/Proxy/throw scenarios passed.`);
