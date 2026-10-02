import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';
const source = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8'});
const generated = runInNewContext(source);
const reference = (left, right) => left instanceof right;
function observe(fn, scenario, failAt) {
  const events = [], failure = {};
  let steps = 0;
  const event = name => { events.push(name); if (++steps === failAt) throw failure; };
  function Base() {}
  let left = Object.create(Base.prototype), right = Base;
  switch (scenario) {
    case 0: break;
    case 1: left = null; break;
    case 2: left = 42; break;
    case 3: right = 42; break;
    case 4: right = {}; break;
    case 5: case 6: case 7: case 8:
      right = {get [Symbol.hasInstance]() {
        event('get:hasInstance');
        return function(value) {
          assert.equal(this, right); assert.equal(value, left); event('call:hasInstance');
          return scenario === 5 ? false : scenario === 6 ? 0 : scenario === 7 ? Symbol('truthy') :
            {[Symbol.toPrimitive]() { assert.fail('hasInstance result must use ToBoolean'); }};
        };
      }}; break;
    case 9:
      right = new Proxy(Base, {get(target, key, receiver) {
        event(`get:${String(key)}`); return Reflect.get(target, key, receiver);
      }}); break;
    case 10:
      left = new Proxy(left, {getPrototypeOf(target) { event('getPrototypeOf'); return Reflect.getPrototypeOf(target); }}); break;
    case 11: {
      const revoked = Proxy.revocable(Base, {}); revoked.revoke(); right = revoked.proxy; break;
    }
    case 12: right = {[Symbol.hasInstance]: 3}; break;
    case 13: right = Base.bind(null); break;
    case 14: right = () => {}; break;
    case 15: Object.defineProperty(Base, 'prototype', {value: 3}); break;
  }
  try { events.push(['return', fn(left, right)]); }
  catch (error) {
    if (error === failure) events.push('injected');
    else { assert.equal(error.name, 'TypeError'); events.push('TypeError'); }
  }
  return events;
}
let cases = 0;
for (let scenario = 0; scenario < 16; ++scenario) for (let failAt = 0; failAt <= 8; ++failAt) {
  assert.deepEqual(observe(generated, scenario, failAt), observe(reference, scenario, failAt)); ++cases;
}
console.log(`Native instanceof: ${cases} hasInstance/Proxy/exception scenarios passed`);
