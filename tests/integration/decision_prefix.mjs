import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';

const cases = [
  ['--source', false, target => target.method(target.probe || ((target.flag ? target.left : target.right) === target.test) ? target.yes : target.no)],
  ['--arm-source', false, target => target.method(target.probe ? ((target.flag ? target.left : target.right) === target.test) : target.no)],
  ['--leading-source', false, target => target.method((target.leadingFlag ? target.probe : target.leadingOther) ||
    ((target.flag ? target.left : target.right) === target.test) ? target.yes : target.no)],
  ['--object-source', true, target => target.method({item: (target.leadingFlag ? target.probe : target.leadingOther) ||
    ((target.flag ? target.left : target.right) === target.test) ? target.yes : target.no})],
  ['--logical-source', false, target => target.method(target.probe || ((target.flag ? target.left : target.right) === target.test))],
  ['--logical-object-source', true, target => target.method({item: (target.leadingFlag ? target.probe : target.leadingOther) ||
    ((target.flag ? target.left : target.right) === target.test)})]
];
const failure = {}, yes = {}, no = {}, result = {}, token = {}, symbol = Symbol('value');
const hostile = {[Symbol.toPrimitive]() { assert.fail('unexpected coercion'); }};
const probes = [undefined, null, false, -0, NaN, true, symbol, hostile];
const pairs = [[0, -0], [NaN, NaN], [token, token], [token, {}], [symbol, symbol], [undefined, null]];
function run(fn, wrapped, probe, flag, left, test, options) {
  const events = []; let steps = 0, changed = false;
  const event = name => { events.push(name); if (++steps === options.failAt) throw failure; };
  const target = {
    get method() {
      event('method'); const call = function(value) {
        assert.equal(this, target); event('call');
        if (wrapped) {
          assert.deepEqual(Object.keys(value), ['item']);
          const descriptor = Object.getOwnPropertyDescriptor(value, 'item');
          assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
          value = descriptor.value;
        }
        events.push(['argument', value]); return result;
      };
      Object.defineProperty(call, 'call', {get() { assert.fail('mutable call helper'); }}); return call;
    },
    get probe() {
      event('probe');
      if (options.replace) Object.defineProperty(target, 'method', {value() { assert.fail('callee reread'); }});
      return probe;
    },
    get flag() { event('flag'); return flag; },
    get leadingFlag() { event('leadingFlag'); return flag; },
    get leadingOther() { event('leadingOther'); return undefined; },
    get left() { event('left'); changed = options.change; return left; },
    get right() { event('right'); changed = options.change; return token; },
    get test() { event('test'); return changed ? left : test; },
    get yes() { event('yes'); return yes; },
    get no() { event('no'); return no; }
  };
  try { assert.equal(fn(target), result); events.push('return'); }
  catch (error) { assert.equal(error, failure); events.push('throw'); }
  return events;
}
let scenarios = 0;
for (const [option, wrapped, reference] of cases) {
const source = execFileSync(process.argv[2], [option], {encoding: 'utf8'});
assert.ok(source.includes(' ? ') && !source.includes('INCOMPLETE'));
const generated = runInNewContext(source);
for (const probe of probes) for (const flag of [false, true]) for (const [left, test] of pairs)
  for (const options of [{}, {replace: true}, {change: true}, ...Array.from({length: 12}, (_, i) => ({failAt: i + 1}))]) {
    assert.deepEqual(run(generated, wrapped, probe, flag, left, test, options), run(reference, wrapped, probe, flag, left, test, options));
    ++scenarios;
  }
}
console.log(`Conditional decision prefixes: ${scenarios} independent effect/reference scenarios passed`);
