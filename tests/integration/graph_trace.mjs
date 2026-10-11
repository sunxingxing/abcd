import assert from 'node:assert/strict';
import {AssertionError} from 'node:assert';
import {types} from 'node:util';

// Test-only observable host boundary. Identity tags never substitute a body;
// complete traces and descriptor snapshots remain independent comparison data.
export class GraphTrace {
  constructor(config = {}, onEvent = () => {}) {
    this.config = config;
    this.onEvent = onEvent;
    this.tags = new WeakMap();
    this.trace = [];
    this.errors = [];
    this.sites = [];
    this.events = 0;
    this.failed = false;
    this.failure = Object.hasOwn(config, 'failure') ? config.failure : {};
  }
  tag(value, name) { this.tags.set(value, name); return value; }
  normalize(value, seen = new Set()) {
    if (value === null || typeof value !== 'object' && typeof value !== 'function') return value;
    if (this.tags.has(value)) return this.tags.get(value);
    if (typeof value === 'function') return ['function', value.name, value.length, Object.hasOwn(value, 'prototype')];
    assert.ok(!seen.has(value), 'untagged cyclic host value');
    seen.add(value);
    const result = Reflect.ownKeys(value).map(key => {
      const d = Object.getOwnPropertyDescriptor(value, key);
      return 'value' in d
        ? [key, this.normalize(d.value, seen), d.writable, d.enumerable, d.configurable]
        : [key, this.normalize(d.get, seen), this.normalize(d.set, seen), d.enumerable, d.configurable];
    });
    seen.delete(value);
    return result;
  }
  exports(modules, intrinsics = null) {
    const ids = new Map(), work = [], roots = [], nodes = [];
    const reference = value => {
      if (value === null || typeof value !== 'object' && typeof value !== 'function') return ['value', value];
      if (intrinsics?.has(value)) return ['intrinsic', intrinsics.get(value)];
      if (!ids.has(value)) {ids.set(value, ids.size); work.push(value);}
      return ['reference', ids.get(value)];
    };
    for (const [name, module] of modules) if (module.status === 'evaluated')
      for (const [key, value] of Object.entries(module.namespace)) roots.push([name, key, reference(value)]);
    for (let i = 0; i < work.length; ++i) {
      const value = work[i];
      const properties = Reflect.ownKeys(value).map(key => {
        const d = Object.getOwnPropertyDescriptor(value, key);
        return 'value' in d ? [key, reference(d.value), d.writable, d.enumerable, d.configurable]
          : [key, reference(d.get), reference(d.set), d.enumerable, d.configurable];
      });
      nodes.push([typeof value, Array.isArray(value), properties,
        ...(intrinsics ? [reference(Object.getPrototypeOf(value)), Object.isExtensible(value)] : [])]);
    }
    // Snapshot content and alias edges before assigning tags. A bad literal or
    // accidentally merged node must fail the snapshot, not hide behind a label.
    for (const [value, id] of ids) if (!this.tags.has(value)) this.tag(value, 'export' + id);
    return {roots, nodes};
  }
  event(name, ...args) {
    this.trace.push([name, ...args.map(value => this.normalize(value))]);
    this.sites.push([++this.events, name, typeof args[0] === 'string' ? args[0] : null]);
    this.onEvent(this.events);
    if (this.events === this.config.failAt) { this.failed = true; throw this.failure; }
  }
  outcome(error) {
    if (Object.is(error, this.failure)) return 'original throw';
    assert.ok(types.isNativeError(error), 'unexpected non-native throw');
    assert.ok(['TypeError', 'ReferenceError', 'RangeError'].includes(error?.name), String(error));
    return error.name;
  }
  call(name, fn, receiver, args = []) {
    try {
      const result = Reflect.apply(fn, receiver, args);
      this.trace.push([name + '.return', this.normalize(result)]);
      return result;
    } catch (error) {
      if (error instanceof AssertionError) throw error;
      if (!Object.is(error, this.failure)) this.errors.push([name, error?.message]);
      this.trace.push([name + '.throw', this.outcome(error)]);
    }
  }
  surface(name, members) {
    let object;
    object = new Proxy({}, {get: (_target, key, receiver) => {
      assert.equal(receiver, object);
      assert.ok(Object.hasOwn(members, key), name + '.' + String(key));
      this.event(name + '.get', key);
      const value = members[key];
      if (typeof value !== 'function') return value;
      const trace = this;
      const fn = function(...args) {
        assert.equal(this, object); trace.event(name + '.call', key, ...args);
        return Reflect.apply(value, object, args);
      };
      for (const adapter of ['call', 'apply', 'bind']) Object.defineProperty(fn, adapter, {
        get() {assert.fail('ambient host invocation adapter');},
      });
      return fn;
    }});
    return this.tag(object, name);
  }
}
