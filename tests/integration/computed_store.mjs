import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], {encoding: 'utf8'});
const generated = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const Case = Object.freeze({
  normal: Symbol('normal'), symbol: Symbol('symbol'), null: Symbol('null'),
  keyThrow: Symbol('key-throw'), rhsThrow: Symbol('rhs-throw'),
  setterThrow: Symbol('setter-throw'), readonly: Symbol('readonly'),
});
// Independent source oracle: RHS completed before the encoded store begins.
function reference(object, key, rhs) {
  const value = rhs();
  object[key] = value;
}
function observe(fn, mode) {
  const events = [];
  const marker = {identity: 'thrown'};
  const value = {identity: 'value'};
  const symbol = Symbol('key');
  const key = {[Symbol.toPrimitive](hint) {
    events.push(['key', hint]);
    if (mode === Case.keyThrow) throw marker;
    return mode === Case.symbol ? symbol : 'field';
  }};
  const target = {};
  const object = mode === Case.null ? null : new Proxy(target, {
    set(base, property, stored, receiver) {
      events.push(['set', property === symbol ? 'symbol' : property,
        stored === value, receiver === object]);
      if (mode === Case.setterThrow) throw marker;
      if (mode === Case.readonly) return false;
      base[property] = stored;
      return true;
    },
  });
  const rhs = () => {
    events.push(['rhs']);
    if (mode === Case.rhsThrow) throw marker;
    return value;
  };
  let completion;
  try { completion = ['return', fn(object, key, rhs)]; }
  catch (error) { completion = ['throw', error === marker ? 'marker' : error.constructor.name]; }
  return {events, completion, stored: target[mode === Case.symbol ? symbol : 'field'] === value};
}
for (const fn of [generated.narrow, generated.wide]) {
  assert.equal(fn.length, 3);
  for (const mode of Object.values(Case)) {
    assert.deepEqual(observe(fn, mode), observe(reference, mode), mode.description);
  }
  // Setter may synchronously reenter a closure that observes the already
  // completed RHS; no snapshot may defer that write until after key coercion.
  let state = 0;
  const object = {set x(value) { assert.equal(state, 1); state += value; }};
  const key = {[Symbol.toPrimitive]() { assert.equal(state, 1); return 'x'; }};
  fn(object, key, () => { state = 1; return 2; });
  assert.equal(state, 3);
}

function captureReference(object, key, next) {
  return () => {
    const base = object;
    const property = key;
    const value = next();
    next = value;
    base[property] = value;
  };
}
function observeCaptures(factory, failKey, reenterSetter) {
  const events = [];
  const thrown = {};
  const callbacks = [];
  for (let i = 0; i < 3; ++i)
    callbacks.push(() => {
      events.push(['rhs', i]);
      return callbacks[(i + 1) % callbacks.length];
    });
  const keys = [];
  const targets = [];
  const closures = [];
  for (let i = 0; i < 2; ++i) {
    let reads = 0;
    let reentered = false;
    keys.push({[Symbol.toPrimitive]() {
      events.push(['key', i]);
      if (failKey && reads++ === 0) throw thrown;
      return 'field';
    }});
    targets.push(new Proxy({}, {
      set(object, key, value) {
        events.push(['set', i, callbacks.indexOf(value)]);
        if (reenterSetter && !reentered) {
          reentered = true;
          closures[i]();
        }
        object[key] = value;
        return true;
      },
    }));
    closures.push(factory(targets[i], keys[i], callbacks[0]));
    assert.equal(closures[i].length, 0);
    assert.equal(Object.hasOwn(closures[i], 'prototype'), false);
  }
  // Interleave two activations of the same factory. Their cells are distinct;
  // a failed property operation must not undo the earlier captured-cell write.
  for (const index of [0, 1, 0, 1, 0]) {
    try { assert.equal(closures[index](), undefined); }
    catch (error) { assert.equal(error, thrown); events.push(['throw', index]); }
  }
  return {events, values: targets.map(object => callbacks.indexOf(object.field))};
}
for (const factory of [generated.closureNarrow, generated.closureWide]) {
  for (const failKey of [false, true])
    for (const reenterSetter of [false, true])
      assert.deepEqual(observeCaptures(factory, failKey, reenterSetter),
        observeCaptures(captureReference, failKey, reenterSetter));
}
for (const factory of [generated.receiverNarrow, generated.receiverWide]) {
  const bindReceiver = (object, key, next) => {
    // The explicit object argument is deliberately wrong. Only frame.thisObj
    // may seed the captured cell; later child-call receivers must not replace it.
    const closure = Reflect.apply(factory, object, [null, key, next]);
    assert.equal(Object.hasOwn(closure, 'prototype'), false);
    return () => Reflect.apply(closure, {unrelated: true}, []);
  };
  for (const failKey of [false, true])
    for (const reenterSetter of [false, true])
      assert.deepEqual(observeCaptures(bindReceiver, failKey, reenterSetter),
        observeCaptures(captureReference, failKey, reenterSetter));
}
for (const factory of [generated.selfNarrow, generated.selfWide]) {
  assert.equal(factory.name, 'store');
  for (const failKey of [false, true]) {
    for (const reenterSetter of [false, true]) {
      const properties = [];
      const bindSelf = (object, key, next) => {
        // Give each activation a distinct property on the actual function.
        // The forwarding setter proves the captured base is that function,
        // while reusing the independent key/cell/reentry event oracle.
        const property = Symbol();
        properties.push(property);
        Object.defineProperty(factory, property, {configurable: true, set(value) {
          assert.equal(this, factory);
          object.field = value;
        }});
        const ownedKey = {[Symbol.toPrimitive](hint) {
          key[Symbol.toPrimitive](hint);
          return property;
        }};
        const closure = Reflect.apply(factory, {wrong: true}, [null, ownedKey, next]);
        assert.equal(Object.hasOwn(closure, 'prototype'), false);
        return () => Reflect.apply(closure, {alsoWrong: true}, []);
      };
      assert.deepEqual(observeCaptures(bindSelf, failKey, reenterSetter),
        observeCaptures(captureReference, failKey, reenterSetter));
      for (const property of properties) delete factory[property];
    }
  }
}
