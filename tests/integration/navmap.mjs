import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {dirname, join} from 'node:path';
import vm from 'node:vm';

const generated = execFileSync(process.argv[2], [process.argv[3], '--navmap'], {encoding: 'utf8'});
// Paired source belongs only to this independent test process, never the engine.
const paired = readFileSync(join(dirname(process.argv[3]),
  'Immersive_src/default/src/main/ets/pages/Index.ts'), 'utf8');
const begin = paired.indexOf('    NavPageMap(name: string, parent = null) {');
const end = paired.indexOf('    initialRender()', begin);
assert.ok(begin >= 0 && end > begin);
const reference = paired.slice(begin, end).replace('name: string', 'name');
const routes = ['Shopping', 'ShoppingAvoid', 'MiniGame', 'ShoppingFullScreen', 'missing', undefined, {}];
let scenarios = 0;
function run(source, route, parent, initial, deferred, failure) {
  const events = [], pending = [];
  let receiver;
  const event = (name, ...args) => {
    events.push([name, ...args]);
    if (failure === name) throw new Error(name);
  };
  const If = {
    create() { assert.equal(this, If); event('if-create'); },
    pop() { assert.equal(this, If); event('if-pop'); },
  };
  const ViewPU = {create(value) {
    assert.equal(this, ViewPU); event('view-create', value.tag);
  }};
  const globals = {If, ViewPU};
  for (const name of routes.slice(0, 4)) {
    globals[name] = class {
      constructor(self, params, storage, id, callback, metadata) {
        assert.equal(self, receiver);
        assert.equal(storage, undefined);
        assert.deepEqual(Object.keys(params), []);
        assert.equal(callback.call({}), undefined);
        this.tag = name;
        event('construct', name, id, metadata.page, metadata.line, metadata.col);
      }
      set paramsGenerator_(generator) {
        assert.equal(generator.name, 'paramsLambda');
        const first = generator.call({}), second = generator.call({});
        assert.deepEqual(Object.keys(first), []);
        assert.notEqual(first, second);
        event('generator', this.tag);
      }
    };
  }
  const method = vm.runInNewContext(`(class { ${source} }).prototype.NavPageMap`, globals);
  assert.equal(method.length, 1);
  receiver = {
    observeComponentCreation2(callback, kind) {
      assert.equal(this, receiver);
      event('observe', kind === If ? 'If' : kind.name);
      const invoke = () => callback.call({wrongReceiver: true}, 17, initial);
      if (deferred) pending.push(invoke); else invoke();
    },
    ifElseBranchUpdateFunction(index, callback) {
      assert.equal(this, receiver); event('branch', index);
      callback.call({wrongReceiver: true});
    },
    updateStateVarsOfChildByElmtId(id, params) {
      assert.equal(this, receiver); assert.deepEqual(Object.keys(params), []);
      event('update', id);
    },
  };
  try {
    assert.equal(method.call(receiver, route, parent), undefined);
    while (pending.length) pending.shift()();
    events.push(['return']);
  } catch (error) {
    // Assertion failures are test failures, not simulated program exceptions.
    if (error.code === 'ERR_ASSERTION') throw error;
    events.push(['throw', error.message]);
  }
  return events;
}
for (const route of routes) for (const parent of [undefined, null, {}])
  for (const initial of [false, true]) for (const deferred of [false, true])
    for (const failure of ['', 'observe', 'if-create', 'if-pop', 'branch', 'construct', 'view-create', 'generator', 'update']) {
      assert.deepEqual(run(generated, route, parent, initial, deferred, failure),
        run(reference, route, parent, initial, deferred, failure));
      ++scenarios;
    }
console.log(`NavPageMap: ${scenarios} paired-source behavior scenarios passed`);
