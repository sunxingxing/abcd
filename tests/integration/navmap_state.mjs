import assert from 'node:assert/strict';
import vm from 'node:vm';

// Independent route method oracle shared by legacy and actual ValueTree record
// tests. The actual loader must return the method from the generated module.
export async function verifyNavMap(loadMethod, reference) {
  const routes = ['Shopping', 'ShoppingAvoid', 'MiniGame', 'ShoppingFullScreen', 'missing', undefined, {}];
  let scenarios = 0;
  async function run(loadMethod, route, parent, initial, deferred, failure) {
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
    class ViewPU {
      static create(value) {
        assert.equal(this, ViewPU); event('view-create', value.tag);
      }
    }
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
    const context = vm.createContext(globals);
    const method = await loadMethod(context, globals);
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
        assert.deepEqual(await run(loadMethod, route, parent, initial, deferred, failure),
          await run(context => vm.runInContext(`(class { ${reference} }).prototype.NavPageMap`, context), route, parent, initial, deferred, failure));
        ++scenarios;
      }
  return scenarios;
}
