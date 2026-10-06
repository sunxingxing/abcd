import assert from 'node:assert/strict';
import vm from 'node:vm';

// Independent native constructor execution. The complete emitted module is
// evaluated first; the real constructor is retained through new.target before
// the platform launch is stopped. Its actual methods remain in the call path.
async function construct(source, mode, behavior, alternate, lambda, elmtId, failAt = '', failure) {
  const trace = [], points = new Set(), fields = new Map();
  const parentArg = {}, params = {}, storage = {}, extra = {}, stop = {};
  const args = [parentArg, params, storage, elmtId, lambda, extra];
  let C, requested, receiver, captured = false, token;
  const tag = value => {
    const index = args.findIndex(input => Object.is(input, value));
    if (index >= 0) return ['argument', index];
    if (Object.is(value, -0)) return ['number', '-0'];
    if (Number.isNaN(value)) return ['number', 'NaN'];
    if (value === undefined) return ['undefined'];
    return [typeof value, value];
  };
  const event = (label, ...values) => {
    trace.push([label, ...values]); points.add(label);
    if (label === failAt) throw failure;
  };
  function Base(...values) {
    if (!captured) { C = new.target; captured = true; throw stop; }
    assert.equal(new.target, requested);
    const incoming = mode === 'bound' ? values.slice(1) : values;
    event('base', Array.from(incoming, tag), new.target === C ? 'class' : 'alternate');
    receiver = behavior === 'object' ? token : this;
    if (behavior === 'throw') throw failure;
    return behavior === 'object' ? token : 5;
  }
  Object.defineProperty(Base.prototype, 'createLocalStorageProp', {configurable: true, get() {
    assert.equal(this, receiver); event('create:get');
    return function(key, initial, alias) {
      assert.equal(this, receiver); event(`create:${key}`, initial, alias);
      const value = {key}; fields.set(value, key); return value;
    };
  }});
  Object.defineProperty(Base.prototype, 'finalizeConstruction', {configurable: true, get() {
    assert.equal(this, receiver); event('finalize:get');
    return function() { assert.equal(this, receiver); event('finalize:call', arguments.length); };
  }});
  const context = vm.createContext({ViewPU: Base, LocalStorage: function() {},
    ViewStackProcessor: {AllocateNewElmetIdForNextComponent() { return 1; },
      StartGetAccessRecordingFor() {}, StopGetAccessRecording() {}},
    loadEtsCard() { assert.fail('launch must stop at first base invocation'); }});
  const root = new vm.SourceTextModule(source, {context});
  await root.link(() => assert.fail('unexpected dependency'));
  await assert.rejects(root.evaluate(), error => error === stop);
  assert.equal(C.name, 'WidgetCard'); assert.equal(C.length, 3);
  const initialize = C.prototype.setInitiallyProvidedValue;
  assert.equal(typeof initialize, 'function');
  Object.defineProperty(C.prototype, 'setInitiallyProvidedValue', {configurable: true, get() {
    assert.equal(this, receiver); event('initialize:get');
    return function(...values) {
      assert.equal(this, receiver); event('initialize:call', Array.from(values, tag));
      return Reflect.apply(initialize, this, values);
    };
  }});
  let parent = mode === 'bound' ? Base.bind(null, 'prefix') : Base;
  if (mode === 'proxy') parent = new Proxy(Base, {construct(target, values, newTarget) {
    // Argument lists cross VM realms; compare each actual argument identity,
    // rather than the realm prototype of the host trap's transport array.
    event('construct-trap', Array.from(values, tag)); return Reflect.construct(target, values, newTarget);
  }});
  Object.setPrototypeOf(C, parent);
  function Target() {}
  Target.prototype = Object.create(C.prototype);
  requested = alternate ? Target : C;
  token = Object.create(C.prototype);
  let value, raised = false, thrown;
  try { value = Reflect.construct(C, args, requested); }
  catch (error) { raised = true; thrown = error; }
  if (raised) assert.equal(thrown, failure);
  else {
    assert.equal(value, receiver);
    assert.equal(Object.getPrototypeOf(value), behavior === 'object' ? C.prototype : requested.prototype);
  }
  const state = receiver ? Reflect.ownKeys(receiver).map(key => {
    const descriptor = Object.getOwnPropertyDescriptor(receiver, key);
    return [key, fields.has(descriptor.value) ? ['field', fields.get(descriptor.value)] : tag(descriptor.value),
      descriptor.writable, descriptor.enumerable, descriptor.configurable];
  }) : [];
  return {trace, state, raised, returnedObject: !raised && value === token, points: [...points]};
}

export async function verifyWidgetConstructors(source, reference) {
  const lambda = () => {};
  let pairedCases = 0;
  for (const mode of ['direct', 'bound', 'proxy']) for (const behavior of ['primitive', 'object', 'throw'])
    for (const alternate of [false, true]) for (const parameter of [undefined, null, 'text', lambda])
      for (const elmtId of [undefined, -1, 0, -0, NaN, 7]) {
        const failure = {};
        assert.deepEqual(await construct(source, mode, behavior, alternate, parameter, elmtId, '', failure),
          await construct(reference, mode, behavior, alternate, parameter, elmtId, '', failure));
        ++pairedCases;
      }
  const baseline = await construct(source, 'proxy', 'object', true, lambda, undefined);
  let throwCases = 0;
  for (const point of baseline.points) for (const failure of [undefined, 0, {}]) {
    assert.deepEqual(await construct(source, 'proxy', 'object', true, lambda, undefined, point, failure),
      await construct(reference, 'proxy', 'object', true, lambda, undefined, point, failure));
    ++throwCases;
  }
  let mutations = 0;
  for (const [from, to] of [
    ['this.__text = this.createLocalStorageProp', 'this.__text = (0, this.createLocalStorageProp)'],
    ['this.setInitiallyProvidedValue(params)', 'this.setInitiallyProvidedValue(undefined)'],
    ['this.finalizeConstruction();', 'this.finalizeConstruction(); return ({});'],
  ]) {
    const changed = source.replace(from, to); assert.notEqual(changed, source);
    new vm.SourceTextModule(changed);
    await assert.rejects(construct(changed, 'proxy', 'object', true, lambda, undefined).then(actual =>
      assert.deepEqual(actual, baseline)), {name: 'AssertionError'});
    ++mutations;
  }
  return {pairedCases, throwCases, mutations};
}
