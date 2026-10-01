import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, abc, paired] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '45ffdc7d5075b4de3f1c6860dcbc929bae048097af4478d09136afbec1b6c5a8');
const run = spawnSync(tool, [abc, '30756', '--record-source', '--value-source'], {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr + run.stdout);
const report = JSON.parse(run.stdout);
assert.equal(report.route, 'value_tree'); assert.equal(report.legacy_writer_used, false);
assert.equal(report.source_complete, true);
const reference = stripTypeScriptTypes(readFileSync(paired, 'utf8'));
async function observe(source, size, failedAt) {
  const context = vm.createContext({});
  const trace = [], items = Array.from({length: size}, (_, index) => ({index}));
  const failure = {};
  function CardItem(item, index) {
    trace.push([index, item]);
    assert.equal(new.target, CardItem);
    assert.equal(item, items[index]);
    if (index === failedAt) throw failure;
    this.item = item; this.index = index;
  }
  const list = {forEach(callback) {
    assert.equal(this, list);
    assert.equal(callback.name, ''); assert.equal(callback.length, 2);
    for (let index = 0; index < items.length; ++index)
      Reflect.apply(callback, {cardArray: 'wrong callback receiver'}, [items[index], index]);
  }};
  const root = new vm.SourceTextModule(source, {context});
  const base = new vm.SyntheticModule(['CARD_LIST'], function() { this.setExport('CARD_LIST', list); }, {context});
  const detail = new vm.SyntheticModule(['CardItem'], function() { this.setExport('CardItem', CardItem); }, {context});
  await root.link(name => {
    assert.ok(name === '@normalized:N&&&base/Index&1.0.0' ||
      name === '@normalized:N&&&detail/src/main/ets/viewmodel/CardViewModel&1.0.0');
    return name.includes('CardViewModel') ? detail : base;
  });
  assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
  assert.throws(() => root.namespace.CardArray, {name: 'ReferenceError'});
  if (failedAt >= 0 && failedAt < size) {
    await assert.rejects(root.evaluate(), error => error === failure);
    assert.equal(trace.length, failedAt + 1);
    assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
    return;
  }
  await root.evaluate();
  assert.deepEqual(Object.keys(root.namespace), ['CardArray', 'default']);
  const Class = root.namespace.CardArray, singleton = root.namespace.default;
  assert.equal(Class.name, 'CardArray'); assert.equal(Class.length, 0);
  assert.equal(singleton.constructor, Class); assert.equal(singleton.getInstance(), singleton);
  assert.equal(Class.prototype.getInstance.call(singleton), singleton);
  assert.equal(Object.getPrototypeOf(singleton), Class.prototype);
  assert.equal(Object.getOwnPropertyDescriptor(Class.prototype, 'getInstance').enumerable, false);
  const check = instance => {
    assert.deepEqual(Object.keys(instance), ['cardArray']);
    const array = instance.cardArray;
    assert.equal(Object.getPrototypeOf(array), vm.runInContext('Array.prototype', context));
    assert.equal(array.length, size);
    for (let i = 0; i < size; ++i) {
      assert.equal(array[i].item, items[i]); assert.equal(array[i].index, i);
      assert.equal(Object.getPrototypeOf(array[i]), CardItem.prototype);
    }
  };
  check(singleton);
  const second = new Class(); check(second);
  assert.notEqual(second, singleton); assert.notEqual(second.cardArray, singleton.cardArray);
  for (let i = 0; i < size; ++i) assert.notEqual(second.cardArray[i], singleton.cardArray[i]);
  assert.equal(root.namespace.default, singleton);
  assert.equal(trace.length, size * 2);
}
let scenarios = 0;
for (const size of [0, 1, 2, 200, 201, 400]) {
  for (const failedAt of [-1, 0, Math.floor(size / 2), size - 1]) {
    await observe(report.source, size, failedAt);
    await observe(reference, size, failedAt);
    ++scenarios;
  }
}
const mutation = report.source.replace('new (CardItem)(item, index)', 'new (CardItem)(item, 0)');
assert.notEqual(mutation, report.source); new vm.SourceTextModule(mutation);
await assert.rejects(observe(mutation, 2, -1));
assert.match(report.source, /\(item, index\) =>/);
console.log(`paired CardArray: ${scenarios} mock-host semantic scenarios, 1 valid-syntax mutation; whole-record style pending`);
