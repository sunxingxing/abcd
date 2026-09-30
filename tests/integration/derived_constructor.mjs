import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const source = execFileSync(process.argv[2], [process.argv[3], '--derived'], {encoding: 'utf8'});
// The reference is an independent behavioral contract. It is never supplied to
// the C++ engine. Framework substitutes expose order, receiver and exceptions.
function run(generated, options) {
  const events = [];
  const params = {marker: 'params'};
  const generator = options.generator === undefined ? undefined : options.generator ? () => params : 7;
  let receiver;
  let target;
  const record = (kind, self, ...args) => {
    assert.equal(self, receiver);
    events.push([kind, ...args]);
    if (options.fail === kind) throw new Error(kind);
  };
  const methods = {
    addProvidedVar(name, value, flag) { record('provide', this, name, value.tag, flag); },
    createStorageLink(name, initial, key) { record('storage', this, name, initial, key); return {tag: 'link'}; },
    setInitiallyProvidedValue(value) { assert.equal(value, params); record('params', this); },
    finalizeConstruction() { record('finalize', this); },
  };
  class Parent {
    constructor(parent, storage, id, extra) {
      target = new.target;
      events.push(['super', parent, storage, id, extra]);
      if (options.fail === 'super') throw new Error('super');
      receiver = options.alternate ? Object.assign({}, methods) : this;
      for (const [name, event] of [['paramsGenerator_', 'setGenerator'], ['__navPageInfos', 'setNav'], ['__windowUtil', 'setWindow']]) {
        let value;
        Object.defineProperty(receiver, name, {get() { return value; }, set(next) {
          record(event, this); value = next;
        }});
      }
      return receiver;
    }
  }
  Object.assign(Parent.prototype, methods);
  class NavPathStack {
    constructor() { events.push(['path']); if (options.fail === 'path') throw new Error('path'); this.tag = 'path'; }
  }
  class ObservedPropertyObjectPU {
    constructor(value, owner, name) {
      record('observed', owner, value.tag, name);
      this.tag = 'observed';
    }
  }
  class Reference extends Parent {
    constructor(parent, params, storage, id = -1, generator = undefined, extra) {
      super(parent, storage, id, extra);
      if (typeof generator === 'function') this.paramsGenerator_ = generator;
      this.__navPageInfos = new ObservedPropertyObjectPU(new NavPathStack(), this, 'navPageInfos');
      this.addProvidedVar('navPageInfos', this.__navPageInfos, false);
      this.__windowUtil = this.createStorageLink('windowUtil', undefined, 'windowUtil');
      this.setInitiallyProvidedValue(params);
      this.finalizeConstruction();
    }
  }
  const Type = generated ? vm.runInNewContext(`(class Recovered extends Parent {${source}})`,
      {Parent, NavPathStack, ObservedPropertyObjectPU}) : Reference;
  assert.equal(Type.length, 3);
  assert.throws(() => Type(), /class constructor|Class constructor/);
  let result;
  let failure;
  try { result = new Type('parent', params, 'storage', options.id, generator, 'extra'); }
  catch (error) { failure = error.message; }
  assert.equal(target, Type);
  if (!failure) {
    assert.equal(result, receiver);
    assert.equal(result.__navPageInfos.tag, 'observed');
    assert.equal(result.__windowUtil.tag, 'link');
    assert.equal(result.paramsGenerator_, options.generator ? generator : undefined);
  }
  return {events, failure, completed: result !== undefined};
}
for (const alternate of [false, true]) for (const generator of [undefined, false, true]) {
  for (const id of [undefined, null, 0, 42]) for (const fail of [undefined, 'super', 'path', 'observed', 'provide', 'storage', 'params', 'finalize', 'setGenerator', 'setNav', 'setWindow']) {
    const options = {alternate, generator, id, fail};
    assert.deepEqual(run(true, options), run(false, options));
  }
}
