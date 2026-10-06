import assert from 'node:assert/strict';
import {types} from 'node:util';

// Test-only external request queue. Drive pending async parents by actual
// request/reaction activity; a fixed tick count or allSettled on the parent
// cannot release a request it has not created yet. Observer reactions use the
// host native prototype, outside the application realm being observed.
export class NativeRequests {
  constructor(trace, config, rejection, outcome) {
    this.trace = trace; this.config = config; this.rejection = rejection; this.outcome = outcome;
    this.requests = []; this.owned = []; this.tracked = new WeakSet();
  }
  changed() {const notify = this.pulse; this.pulse = undefined; if (notify) notify();}
  track(promise) {
    if (promise === undefined) return promise;
    assert.ok(types.isPromise(promise));
    if (this.tracked.has(promise)) return promise;
    this.tracked.add(promise); const entry = {promise, state: 'pending'}; this.owned.push(entry);
    Promise.prototype.then.call(promise, value => {entry.state = 'fulfilled'; entry.value = value; this.changed();},
      error => {entry.state = 'rejected'; entry.value = error; this.changed();});
    return promise;
  }
  monitor(promise, id) {
    this.track(promise);
    return this.trace.surface('promise' + id, Object.fromEntries(['then', 'catch'].map(name => [name,
      (...callbacks) => this.monitor(Reflect.apply(Promise.prototype[name], promise, callbacks), id)])));
  }
  request(kind, result) {
    const entry = {kind, id: this.requests.length, released: false}; this.requests.push(entry);
    const promise = new Promise((resolve, reject) => {entry.resolve = resolve; entry.reject = reject;});
    entry.result = result; const wrapper = this.monitor(promise, entry.id); this.changed();
    this.trace.event('platform.request', kind, entry.id); return wrapper;
  }
  observeReactions(prototype) {
    const nativeThen = prototype.then, queue = this;
    const then = {then(onFulfilled, onRejected) {
      return queue.track(Reflect.apply(nativeThen, this, [onFulfilled, onRejected]));
    }}.then;
    Object.defineProperty(prototype, 'then', {value: then, configurable: true, writable: true});
  }
  async finish() {
    while (this.owned.some(entry => entry.state === 'pending') || this.requests.some(entry => !entry.released)) {
      const available = this.requests.filter(entry => !entry.released);
      for (const entry of this.config.reverse ? available.reverse() : available) {
        entry.released = true; this.trace.trace.push(['platform.release', entry.kind, entry.id]);
        if (entry.kind === this.config.reject) entry.reject(this.rejection); else entry.resolve(entry.result);
      }
      if (this.owned.some(entry => entry.state === 'pending')) await new Promise(resolve => {this.pulse = resolve;});
    }
    await Promise.allSettled(this.owned.map(entry => entry.promise));
  }
  snapshot() {
    return {completions: this.owned.map(entry => entry.state === 'fulfilled'
      ? ['fulfilled', this.trace.normalize(entry.value)] : ['rejected', this.outcome(entry.value)]),
      requests: this.requests.map(({kind, released}) => [kind, released])};
  }
}
