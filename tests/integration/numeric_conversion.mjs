import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';

// Independent abstract-operation oracle using captured test-realm intrinsics.
const apply = Reflect.apply;
const primitive = value => value === null || (typeof value !== 'object' && typeof value !== 'function');
function toNumeric(value) {
  let result = value;
  if (!primitive(value)) {
    const exotic = value[Symbol.toPrimitive];
    if (exotic !== null && exotic !== undefined) {
      if (typeof exotic !== 'function') throw new TypeError();
      result = apply(exotic, value, ['number']);
      if (!primitive(result)) throw new TypeError();
    } else {
      let found = false;
      for (const key of ['valueOf', 'toString']) {
        const method = value[key];
        if (typeof method !== 'function') continue;
        result = apply(method, value, []);
        if (primitive(result)) { found = true; break; }
      }
      if (!found) throw new TypeError();
    }
  }
  return typeof result === 'bigint' ? result : +result;
}

function exercise(fn, scenario, failAt) {
  const events = [], failure = {};
  let steps = 0, calls = 0;
  const event = label => { events.push(label); if (++steps === failAt) throw failure; };
  const primitives = [undefined, null, false, true, 0, -0, NaN, Infinity, -Infinity,
    '', '  ', '0x10', '-0', '12.5e2', 'Infinity', 'bad', 1n, Symbol('value')];
  let input;
  if (scenario < primitives.length) input = primitives[scenario];
  else switch (scenario - primitives.length) {
    case 0:
      input = {get [Symbol.toPrimitive]() {
        event('get:primitive');
        return function(hint) { assert.equal(this, input); assert.equal(hint, 'number'); event('call:primitive'); return ++calls; };
      }}; break;
    case 1:
      input = {valueOf() { assert.equal(this, input); event('valueOf'); return '-0'; },
        toString() { assert.fail('unexpected toString'); }}; break;
    case 2:
      input = {valueOf() { assert.equal(this, input); event('valueOf'); return {}; },
        toString() { assert.equal(this, input); event('toString'); return '15'; }}; break;
    case 3: input = {[Symbol.toPrimitive](hint) { assert.equal(hint, 'number'); event('primitive'); return 2n; }}; break;
    case 4: input = {[Symbol.toPrimitive]() { event('primitive'); return {}; }}; break;
    case 5: input = {[Symbol.toPrimitive]: 7}; break;
    case 6: input = Object(3n); break;
    case 7: input = Object(Symbol('boxed')); break;
    case 8: {
      const revoked = Proxy.revocable({}, {}); revoked.revoke(); input = revoked.proxy; break;
    }
    case 9:
      input = new Proxy({valueOf() { assert.equal(this, input); event('valueOf'); return 19; }},
        {get(target, key, receiver) { event(`get:${String(key)}`); return Reflect.get(target, key, receiver); }}); break;
  }
  try { const result = fn(input); events.push(['return', result === input && (typeof input === 'symbol' || (input !== null && typeof input === 'object')) ? '<input>' : result]); }
  catch (error) {
    if (error === failure) events.push('injected');
    else { assert.equal(error.name, 'TypeError'); events.push('TypeError'); }
  }
  return events;
}
let scenarios = 0;
for (const option of ['--source', '--legacy-source', '--phased-source', '--phased-legacy-source']) for (const count of [1, 2, 200, 201, 402]) {
  const source = execFileSync(process.argv[2], [option, String(count)], {encoding: 'utf8'});
  const context = {};
  Object.defineProperty(context, 'Number', {get() { assert.fail('mutable Number helper'); }});
  const generated = runInNewContext(source, context);
  const caught = option.startsWith('--phased');
  const reference = value => {
    try { let result; for (let i = 0; i < count; ++i) result = toNumeric(value); return result; }
    catch (error) { if (caught) return value; throw error; }
  };
  for (let scenario = 0; scenario < 28; ++scenario) for (const failAt of new Set([0, 1, 2, count, count * 2])) {
    assert.deepEqual(exercise(generated, scenario, failAt), exercise(reference, scenario, failAt)); ++scenarios;
  }
  const view = new DataView(new ArrayBuffer(8));
  for (const payload of [0x7ff7000000000005n, 0x7fff000000000005n, 0xfff7000000000005n, 0xfff8000000000000n]) {
    view.setBigUint64(0, payload); const input = view.getFloat64(0);
    view.setFloat64(0, toNumeric(input)); const expected = view.getBigUint64(0);
    view.setFloat64(0, generated(input)); assert.equal(view.getBigUint64(0), expected); ++scenarios;
    let calls = 0;
    const object = {[Symbol.toPrimitive](hint) { assert.equal(hint, 'number'); ++calls; return input; }};
    view.setFloat64(0, generated(object)); assert.equal(view.getBigUint64(0), expected);
    assert.equal(calls, count); ++scenarios;
  }
}
console.log(`Native numeric conversion: ${scenarios} operand/coercion/exception/payload scenarios passed`);
