import assert from 'node:assert/strict';
import { runInNewContext } from 'node:vm';

// Controlled bindings for conditional drafts, not production publication or
// evidence about native module initialization. Expectations describe observable
// behavior of the pinned decoded methods, independently of generated spelling.
export function checkQueryDegree(source) {
  for (const immediate of [false, true]) {
    const events = [];
    const gravity = Symbol('gravity');
    let callback;
    const sensor = {
      get SensorId() { events.push('sensor-id'); return { GRAVITY: gravity }; },
      on(id, action) {
        assert.equal(this, sensor);
        assert.equal(id, gravity);
        events.push('on'); callback = action;
        if (immediate) action({ x: 2, y: 3, z: 4 });
      },
    };
    const hilog = { error() { assert.fail('unexpected log'); } };
    const Fixture = runInNewContext(`class Fixture { ${source} }; Fixture`, { sensor, hilog });
    const instance = new Fixture();
    assert.equal(instance.queryDegree.length, 0);
    instance.getCalDegree = function (...args) {
      assert.equal(this, instance); events.push(['degree', ...args]);
    };
    assert.equal(instance.queryDegree(), undefined);
    assert.deepEqual(events, immediate ? ['sensor-id', 'on', ['degree', 2, 3, 4]] : ['sensor-id', 'on']);
    // Callback sees the same mutable receiver after the parent has returned.
    instance.getCalDegree = function (...args) {
      assert.equal(this, instance); events.push(['updated', ...args]);
    };
    callback.call({ getCalDegree() { assert.fail('dynamic callback receiver'); } }, { x: 5, y: 6, z: 7 });
    assert.deepEqual(events.at(-1), ['updated', 5, 6, 7]);
    const firstCallback = callback;
    assert.equal(firstCallback.length, 1);
    const other = new Fixture();
    other.getCalDegree = function (...args) {
      assert.equal(this, other); events.push(['other', ...args]);
    };
    assert.equal(other.queryDegree(), undefined);
    assert.notEqual(callback, firstCallback);
    firstCallback({ x: 8, y: 9, z: 10 });
    callback({ x: 11, y: 12, z: 13 });
    assert.deepEqual(events.slice(-2), [['updated', 8, 9, 10], ['other', 11, 12, 13]]);
    const failure = { code: 19, message: 'callback' };
    instance.getCalDegree = () => { throw failure; };
    assert.throws(() => firstCallback({ x: 0, y: 0, z: 0 }), error => error === failure);
  }
  for (const fromCallback of [false, true]) {
    const events = [];
    const failure = {
      get code() { events.push('code'); return 17; },
      get message() { events.push('message'); return 'sensor-failure'; },
    };
    const sensor = { SensorId: { GRAVITY: 1 }, on(id, action) {
      events.push('on');
      if (fromCallback) action({ x: 1, y: 2, z: 3 });
      else throw failure;
    } };
    const hilog = { error(...args) { assert.equal(this, hilog); events.push(args); } };
    const Fixture = runInNewContext(`class Fixture { ${source} }; Fixture`, { sensor, hilog });
    const instance = new Fixture();
    instance.getCalDegree = () => { events.push('degree'); throw failure; };
    assert.equal(instance.queryDegree(), undefined);
    assert.deepEqual(events, ['on', ...(fromCallback ? ['degree'] : []), 'code', 'message',
      [0, 'TestLog', 'Failed to query degree. Code: 17, message: sensor-failure']]);
  }
}

export function checkRecover(source) {
  for (const fullscreen of [false, true]) {
    const events = [];
    const status = Symbol('fullscreen');
    const callbacks = [];
    const window = { get WindowStatusType() { events.push('status-type'); return { FULL_SCREEN: status }; } };
    const hilog = {
      info(...args) { assert.equal(this, hilog); events.push(['info', ...args]); },
      error(...args) { assert.equal(this, hilog); events.push(['error', ...args]); },
    };
    const caught = { catch(action) { assert.equal(this, caught); events.push('catch'); callbacks.push(action); } };
    const promise = { then(action) { assert.equal(this, promise); events.push('then'); callbacks.push(action); return caught; } };
    const first = { getWindowStatus() { assert.equal(this, first); events.push('get-status'); return fullscreen ? status : null; } };
    const second = { recover() { assert.equal(this, second); events.push('recover'); return promise; } };
    const Fixture = runInNewContext(`class Fixture { ${source} }; Fixture`, { window, hilog });
    const instance = new Fixture();
    let reads = 0;
    assert.equal(instance.recover.length, 0);
    Object.defineProperty(instance, 'mainWindow', { get() {
      events.push('main-window'); return reads++ === 0 ? first : second;
    } });
    assert.equal(instance.recover(), undefined);
    assert.deepEqual(events, ['main-window', 'get-status', 'status-type',
      ...(fullscreen ? ['main-window', 'recover', 'then', 'catch'] : [])]);
    if (fullscreen) {
      assert.deepEqual(callbacks.map(callback => callback.length), [0, 1]);
      assert.equal(callbacks[0](), undefined);
      assert.deepEqual(events.at(-1), ['info', 0, 'testLog', 'Succeeded in revocering the window.']);
      assert.equal(callbacks[1]({ code: 23, message: 'rejected' }), undefined);
      assert.deepEqual(events.at(-1), ['error', 0, 'testLog', 'Failed to revocer the window. Code: 23, message: rejected']);
      const failure = Symbol('logger-failure');
      hilog.info = () => { throw failure; };
      assert.throws(() => callbacks[0](), error => error === failure);
    }
  }
  for (const getterThrows of [false, true]) {
    const events = [];
    const failure = {
      get code() { events.push('code'); return 31; },
      get message() { events.push('message'); return 'window-failure'; },
    };
    const hilog = { error(...args) { assert.equal(this, hilog); events.push(args); } };
    const Fixture = runInNewContext(`class Fixture { ${source} }; Fixture`, { hilog });
    const instance = new Fixture();
    Object.defineProperty(instance, 'mainWindow', { get() {
      events.push('main-window');
      if (getterThrows) throw failure;
      return { getWindowStatus() { events.push('get-status'); throw failure; } };
    } });
    assert.equal(instance.recover(), undefined);
    assert.deepEqual(events, ['main-window', ...(!getterThrows ? ['get-status'] : []), 'code', 'message',
      [0, 'TestLog', 'Failed to recover. Code: 31, message: window-failure']]);
  }
}
