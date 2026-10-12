import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {dirname, join} from 'node:path';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
import {createHash} from 'node:crypto';
import {verifyIndexRecord} from './index_record_state.mjs';

const [tool, abc] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
const report = JSON.parse(execFileSync(tool, [abc, '10009', '--record-source', '--value-source'], {encoding: 'utf8'}));
assert.equal(report.source_complete, true);
assert.equal(report.route, 'value_tree');
assert.equal(report.legacy_writer_used, undefined);
assert.match(report.source, /class extends \(ViewPU\)/);
assert.equal(report.record, 9572);
const routes = JSON.parse(execFileSync(tool, [abc, '8889', '--record-source', '--value-source'], {encoding: 'utf8'}));
assert.equal(routes.source_complete, true); assert.equal(routes.record, 8755);
assert.equal(routes.route, 'value_tree'); assert.equal(routes.legacy_writer_used, undefined);
// Full paired module is an independent oracle, read only after the ABC-only
// engine process has exited. No method fragments are spliced into its output.
const paired = readFileSync(join(dirname(abc), 'Immersive_src/default/src/main/ets/pages/Index.ts'), 'utf8');
// The pinned producer erases type-only instance declarations. Node strip alone
// materializes them as empty JS fields. Mark only uninitialized typed private
// declarations as declare before the independent parser erases types; retain
// every initializer, constructor assignment and method body. This is oracle
// preparation after BOTH ABC-only exports, never an engine input/answer.
const typeOnly = /^([ \t]*)private\s+([A-Za-z_$][\w$]*)\s*:\s*([^;\n=]+);/gm;
assert.equal([...paired.matchAll(typeOnly)].length, 2);
const reference = stripTypeScriptTypes(paired.replace(typeOnly, '$1declare private $2: $3;'));
const referenceRoutes = stripTypeScriptTypes(readFileSync(join(dirname(abc),
  'Immersive_src/default/src/main/ets/constants/Constants.ts'), 'utf8'));
async function exercise(source, hasFinalize, failure) {
  const trace = [];
  const exception = {};
  const event = (name, ...args) => {
    trace.push([name, ...args]);
    if (failure === name) throw exception;
  };
  let factory, registration, current, lastTarget;
  class ViewPU {
    constructor(parent, storage, id, extra) {
      event('super', parent, storage, id, extra); current = this; lastTarget = new.target;
    }
    addProvidedVar(name, value, flag) { assert.equal(this, current); event('provide', name, flag); }
    createStorageLink(name, value, key) {
      assert.equal(this, current); event('storage', name, key); return new Observed(value, this, name);
    }
    id__() { assert.equal(this, current); event('id'); return 42; }
    aboutToBeDeletedInternal() { assert.equal(this, current); event('deletedInternal'); }
    updateDirtyElements() { assert.equal(this, current); event('dirty'); }
  }
  if (hasFinalize) ViewPU.prototype.finalizeConstruction = function() {
    assert.equal(this, current); event('finalize');
  };
  class NavPathStack {
    constructor() { event('path'); }
    pushPath(value) { event('push', value.name); }
  }
  class Observed {
    constructor(value, owner, name) {
      assert.equal(owner, current); event('observed', name); this.value = value; this.name = name;
    }
    get() { event('get', this.name); return this.value; }
    set(value) { event('set', this.name); this.value = value; }
    purgeDependencyOnElmtId(id) { event('purge', this.name, id); }
    aboutToBeDeleted() { event('delete', this.name); }
  }
  const subscriber = {delete(id) { assert.equal(this, subscriber); event('subscriberDelete', id); }};
  const globals = {
    ViewPU, NavPathStack, ObservedPropertyObjectPU: Observed,
    SubscriberManager: {Get() { event('subscriberGet'); return subscriber; }},
    registerNamedRoute(callback, name, info) {
      assert.equal(this, undefined); event('register', name);
      factory = callback; registration = JSON.parse(JSON.stringify(info));
    },
  };
  const context = vm.createContext(globals);
  const module = new vm.SourceTextModule(source, {context});
  const imports = [];
  await module.link(specifier => {
    imports.push(specifier);
    return new vm.SyntheticModule(['ROUTES', 'Shopping', 'ShoppingAvoid', 'MiniGame', 'ShoppingFullScreen'], function() {
      this.setExport('ROUTES', []);
      for (const name of ['Shopping', 'ShoppingAvoid', 'MiniGame', 'ShoppingFullScreen']) this.setExport(name, class {});
    }, {context});
  });
  try {
    await module.evaluate();
    assert.equal(typeof ViewPU.prototype.finalizeConstruction, 'function');
    assert.equal(factory.length, 0);
    const instance = factory.call({wrongReceiver: true});
    const Type = instance.constructor;
    assert.equal(Type, lastTarget);
    assert.equal(Type.name, 'Index'); assert.equal(Type.length, 3);
    assert.equal(Type.getEntryName(), 'Index');
    assert.equal(Object.getPrototypeOf(Type), ViewPU);
    assert.equal(Object.getPrototypeOf(Type.prototype), ViewPU.prototype);
    assert.throws(() => Type(), error => error.name === 'TypeError');
    instance.go(undefined); instance.go('Shopping');
    const custom = new NavPathStack();
    instance.navPageInfos = custom; assert.equal(instance.navPageInfos, custom);
    const window = {}; instance.windowUtil = window; assert.equal(instance.windowUtil, window);
    instance.updateStateVars({}); instance.purgeVariableDependenciesOnElmtId(7);
    instance.rerender(); instance.aboutToBeDeleted();
    const generator = () => ({});
    const second = new Type('parent', {navPageInfos: custom}, 'storage', 0, generator, 'extra');
    assert.notEqual(second, instance); assert.equal(second.navPageInfos, custom);
    assert.equal(second.paramsGenerator_, generator);
    trace.push(['return']);
  } catch (error) {
    assert.equal(error, exception, 'unexpected complete-record runtime failure');
    trace.push(['throw']);
  }
  return {trace, registration, imports};
}
let scenarios = 0;
for (const finalize of [false, true]) for (const failure of ['', 'register', 'super', 'path', 'observed',
  'provide', 'storage', 'finalize', 'get', 'push', 'set', 'purge', 'dirty', 'delete', 'subscriberGet',
  'id', 'subscriberDelete', 'deletedInternal']) {
  assert.deepEqual(await exercise(report.source, finalize, failure), await exercise(reference, finalize, failure));
  ++scenarios;
}
console.log(`Index complete record: ${scenarios} paired module/constructor/member scenarios passed`);
console.log(JSON.stringify(await verifyIndexRecord(report.source, routes.source, reference, referenceRoutes)));
