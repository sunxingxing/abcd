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
