import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';

const [tool] = process.argv.slice(2);
const shallow = target => {
  target.method({item: {item: target.flag ? target.left : target.right}});
  return target.method({item: {item: target.flag ? target.left : target.right}});
};
const deep = target => {
  target.method({item: {item: {item: {item: {item: {item: {item: target.flag ? target.left : target.right}}}}}}});
  return target.method({item: {item: {item: {item: {item: {item: {item: target.flag ? target.left : target.right}}}}}}});
};
const siblings = target => target.method({
  field0: {item: target.flag ? target.left : target.right},
  field1: {item: target.flag ? target.left : target.right}
});
const failure = {}, marker = {}, token = {};
const hostile = {[Symbol.toPrimitive]() { assert.fail('unexpected coercion'); }};
const values = [undefined, null, false, -0, NaN, 1, 2n, Symbol('value'), token, hostile];
function execute(fn, prototype, layout, flag, leaf, options) {
  const events = [], seen = new Set(), hits = new Map();
  let generation = 0;
  const event = name => {
    events.push(name); const count = (hits.get(name) ?? 0) + 1; hits.set(name, count);
    if (name === options.fail && count === options.ordinal) throw failure;
  };
  const target = {
    get method() {
      event('method'); const saved = generation;
      const call = function(object) {
        assert.equal(this, target); event('call'); events.push(['generation', saved]);
        const own = (object, keys) => {
          assert.ok(object !== null && typeof object === 'object' && !seen.has(object)); seen.add(object);
          assert.equal(Object.getPrototypeOf(object), prototype);
          assert.deepEqual(Object.keys(object), keys);
          for (const key of keys) {
            const property = Object.getOwnPropertyDescriptor(object, key);
            assert.ok(property.writable && property.enumerable && property.configurable);
          }
        };
        if (layout.siblings) {
          own(object, ['field0', 'field1']);
          for (const child of [object.field0, object.field1]) {
            own(child, ['item']); events.push(['leaf', child.item]);
          }
        } else {
          for (let i = 0; i < layout.depth; ++i) { own(object, ['item']); object = object.item; }
          events.push(['leaf', object]);
        }
        return marker;
      };
      Object.defineProperty(call, 'call', {get() { assert.fail('must retain native Reference'); }});
      return call;
    },
    get flag() {
      event('flag'); if (options.replace) ++generation;
      return options.flip && hits.get('flag') % 2 === 0 ? !flag : flag;
    },
    get left() { event('left'); return leaf; },
    get right() { event('right'); return marker; }
  };
  let result;
  try { result = fn(target); } catch (error) { assert.equal(error, failure); result = failure; }
  return {events, result};
}
let scenarios = 0;
for (const layout of [{option: '--source', depth: 2, oracle: shallow},
  {option: '--deep-source', depth: 7, oracle: deep}, {option: '--siblings-source', siblings: true, oracle: siblings}]) {
  const source = execFileSync(tool, [layout.option], {encoding: 'utf8'});
  assert.ok(!source.includes('if (') && !source.includes('.call(') && !source.includes('$phi'));
  const generated = runInNewContext(`({fn: ${source}, prototype: Object.prototype})`);
  for (const flag of values) for (const leaf of values)
    for (const options of [{}, {replace: true}, {flip: true}, {replace: true, flip: true},
      ...['method', 'flag', 'left', 'right', 'call'].flatMap(fail => [1, 2].map(ordinal => ({fail, ordinal})))]) {
      assert.deepEqual(execute(generated.fn, generated.prototype, layout, flag, leaf, options),
        execute(layout.oracle, Object.prototype, layout, flag, leaf, options));
      ++scenarios;
    }
}
console.log(`Nested selected objects: ${scenarios} independent identity/effect scenarios passed`);
