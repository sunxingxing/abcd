import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const forwarded = process.argv.includes('--forwarded');
const unguarded = process.argv.includes('--unguarded');
const reversed = process.argv.includes('--reversed');
const initialization = process.argv.includes('--reversed-initialization');
const mode = initialization ? (unguarded ? (forwarded ? '--unguarded-reversed-initialization-forward-reference-source' : '--unguarded-reversed-initialization-reference-source') :
  (forwarded ? '--reversed-initialization-forward-reference-source' : '--reversed-initialization-reference-source')) : reversed ? (unguarded ? (forwarded ? '--unguarded-reversed-forward-reference-source' : '--unguarded-reversed-reference-source') :
  (forwarded ? '--reversed-forward-reference-source' : '--reversed-reference-source')) : unguarded ? (forwarded ? '--unguarded-forward-reference-source' : '--unguarded-reference-source') :
  (forwarded ? '--forward-reference-source' : '--reference-source');
const reports = execFileSync(process.argv[2], [mode], {encoding: 'utf8'})
  .trim().split('\n').map(JSON.parse);
assert.equal(reports.length, 3);
const reference = `constructor(argument) { super(argument); this.callback = (() => ${forwarded ? '() => ' : ''}this.action(this.argument)); }`;
let scenarios = 0;
function exercise(body, baseKind, mode, setter, proxy, argument) {
  const trace = [], tags = new WeakMap(), callbacks = new WeakMap();
  const raised = Symbol('same arbitrary exception');
  let Type, expected, instance;
  function normalize(value) {
    if (typeof value === 'function') return ['function', value.name, value.length];
    if (value && typeof value === 'object') return tags.get(value) ?? 'argument';
    return value;
  }
  const event = (name, ...values) => trace.push([name, ...values.map(normalize)]);
  function invoke(callback, target) {
    assert.equal(callback.name, ''); assert.equal(callback.length, 0);
    if (forwarded) {
      callback = Reflect.apply(callback, {wrong: true}, []);
      assert.equal(callback.name, ''); assert.equal(callback.length, 0);
    }
    try {
      const result = Reflect.apply(callback, {wrong: true}, []);
      assert.equal(result, argument); event('result', result);
    } catch (error) {
      if (mode === 'non-callable') {
        assert.equal(error.name, 'TypeError'); event('type-error');
      } else {
        assert.equal(error, raised); event('arbitrary-throw');
      }
    }
    assert.equal(target, expected);
  }
  function configure(target, label) {
    tags.set(target, label);
    function action(value) {
      assert.equal(this, expected); assert.equal(value, argument);
      event('action:call', this, value);
      if (mode === 'call-throw') throw raised;
      return value;
    }
    for (const name of ['call', 'apply', 'bind']) Object.defineProperty(action, name, {get() {
      event('poisoned-helper', name); throw raised;
    }});
    Object.defineProperty(target, 'action', {get() {
      assert.equal(this, expected); event('action:get', this);
      if (mode === 'callee-throw') throw raised;
      return mode === 'non-callable' ? null : action;
    }, configurable: true});
    Object.defineProperty(target, 'argument', {get() {
      assert.equal(this, expected); event('argument:get', this);
      if (mode === 'argument-throw') throw raised;
      if (mode === 'replace-callee') Object.defineProperty(target, 'action', {
        value(value) {
          assert.equal(this, expected); event('replacement-action:call', this, value); throw raised;
        }, configurable: true
      });
      return argument;
    }, configurable: true});
  }
  const replacement = new Proxy({}, {
    get(target, key, actual) {
      assert.equal(actual, replacement); event('replacement:get', key);
      return Reflect.get(target, key, actual);
    },
    set(target, key, value, actual) {
      assert.equal(actual, replacement); event('replacement:set', key, value);
      return Reflect.set(target, key, value, actual);
    }
  });
  configure(replacement, 'replacement');
  function Base(value) {
    assert.equal(new.target, Type); assert.equal(value, argument); event('base', value);
    configure(this, 'receiver'); expected = baseKind === 'object' ? replacement : this;
    if (baseKind === 'throw') throw raised;
    return baseKind === 'object' ? replacement : undefined;
  }
  if (setter !== 'own') for (const target of [Base.prototype, replacement])
    Object.defineProperty(target, 'callback', {set(callback) {
      assert.equal(this, expected); event('callback:set', this, callback);
      callbacks.set(this, callback);
      if (setter === 'immediate') invoke(callback, this);
    }, configurable: true});
  const parent = proxy ? new Proxy(Base, {construct(target, args, actual) {
    assert.equal(actual, Type); event('base:construct', ...args);
    return Reflect.construct(target, args, actual);
  }}) : Base;
  const context = vm.createContext({Parent: parent});
  vm.runInContext(`for (const name of ['call', 'apply', 'bind']) {
    Function.prototype[name] = function() { throw new Error('ambient call helper'); };
  }`, context);
  Type = vm.runInContext(`(class Type extends Parent { ${body} })`, context);
  try {
    instance = new Type(argument);
  } catch (error) {
    assert.equal(baseKind, 'throw'); assert.equal(error, raised); event('base:throw');
    return trace;
  }
  assert.equal(instance, expected);
  const callback = callbacks.get(instance) ?? instance.callback;
  invoke(callback, instance);
  event('keys', Reflect.ownKeys(instance).join(','));
  return trace;
}
for (const {source} of reports) {
  assert.ok(!source.includes('INCOMPLETE'));
  for (const base of ['ordinary', 'object', 'throw'])
    for (const mode of ['return', 'callee-throw', 'argument-throw', 'call-throw', 'non-callable', 'replace-callee'])
      for (const setter of ['own', 'save', 'immediate']) for (const proxy of [false, true])
        for (const argument of [undefined, null, -0, 7n, Symbol('value'), {}, () => {}]) {
          assert.deepEqual(exercise(source, base, mode, setter, proxy, argument),
            exercise(reference, base, mode, setter, proxy, argument)); ++scenarios;
        }
  // Corrupt the receiver cell while preserving the real super invocation and
  // its saved result, whether saving is nested or occurs in an earlier statement.
  const changed = source.replace(
    /(\(\$local\d+ = )(\(\$value\d+ = super\([^)]*\)\)|\$value\d+)(\);)/,
    '$1($2, null)$3');
  assert.notEqual(changed, source);
  assert.doesNotThrow(() => new vm.Script(`(class Type extends Parent { ${changed} })`));
  assert.throws(() => exercise(changed, 'ordinary', 'return', 'own', false, {}));
  const earlyThis = source.replace(' = (void 0));', ' = this);');
  assert.notEqual(earlyThis, source);
  assert.doesNotThrow(() => new vm.Script(`(class Type extends Parent { ${earlyThis} })`));
  assert.throws(() => exercise(earlyThis, 'ordinary', 'return', 'own', false, {}));
}
console.log(`${unguarded ? 'Unguarded captured' : 'Captured'} receiver member calls: ${scenarios} complete constructor/capture/getter/receiver/throw scenarios passed.`);
