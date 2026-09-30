import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
const [tool, abc, paired] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
const run = spawnSync(tool, [abc], {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr);
assert.match(run.stdout, /^constructor\(\)\s*\{/);
assert.doesNotMatch(run.stdout, /\$value|function\s/);
// Only constructor behavior is compared. The reference's static fields are not
// implemented by this fragment and are deliberately outside this assertion.
async function load(source) {
  const module = new vm.SourceTextModule(source);
  await module.link(() => { throw new Error('unexpected dependency'); });
  await module.evaluate(); return module.namespace.CommonConstants;
}
const actual = await load(`export class CommonConstants { ${run.stdout} }`);
const reference = await load(stripTypeScriptTypes(readFileSync(paired, 'utf8')));
function observe(C) {
  assert.throws(() => Reflect.apply(C, {}, []), {name: 'TypeError'});
  const one = new C(), two = new C('ignored');
  assert.notEqual(one, two);
  assert.equal(Object.getPrototypeOf(one), C.prototype);
  assert.equal(Object.getPrototypeOf(two), C.prototype);
  assert.deepEqual(Reflect.ownKeys(one), []);
  class Derived extends C {}
  const child = new Derived();
  assert.equal(Object.getPrototypeOf(child), Derived.prototype);
  assert.ok(child instanceof C);
  const prototype = {};
  let reads = 0;
  const alternate = new Proxy(function() {}, {
    get(target, key, receiver) {
      if (key === 'prototype') { ++reads; return prototype; }
      return Reflect.get(target, key, receiver);
    },
  });
  const redirected = Reflect.construct(C, [], alternate);
  assert.equal(Object.getPrototypeOf(redirected), prototype);
  assert.equal(reads, 1);
  const descriptor = Object.getOwnPropertyDescriptor(C.prototype, 'constructor');
  assert.equal(descriptor.value, C);
  return {length: C.length, writable: descriptor.writable,
    enumerable: descriptor.enumerable, configurable: descriptor.configurable,
    keys: Reflect.ownKeys(child)};
}
assert.deepEqual(observe(actual), observe(reference));

// A second real constructor exercises instance initialization and virtual call.
// Override the dependency method equally in both classes to isolate constructor
// behavior; the method's complete body remains a separate recovery obligation.
const homeRun = spawnSync(tool, [abc, '16638'], {encoding: 'utf8', timeout: 60_000});
assert.equal(homeRun.status, 0, homeRun.stderr);
assert.match(homeRun.stdout, /this\.tabBarList = \[\]/);
assert.match(homeRun.stdout, /this\.initTabBarList\(\)/);
const homePaired = process.argv[5];
async function home(source) {
  const module = new vm.SourceTextModule(source);
  await module.link(() => new vm.SourceTextModule('export const HomeConstants = {};'));
  await module.evaluate(); return module.namespace.HomeModel;
}
const actualHome = await home(`export class HomeModel { ${homeRun.stdout} }`);
const referenceHome = await home(stripTypeScriptTypes(readFileSync(homePaired, 'utf8')));
function homeObservation(C) {
  const events = [];
  const receivers = [];
  Object.defineProperty(C.prototype, 'initTabBarList', {
    configurable: true,
    get() {
      events.push(['get', Array.isArray(this.tabBarList), this.tabBarList.length]);
      const receiver = this;
      return function() {
        assert.equal(this, receiver); receivers.push(this);
        events.push(['call', this.tabBarList.length]);
        this.tabBarList.push('initialized');
      };
    },
  });
  const first = new C(), second = new C();
  assert.equal(receivers[0], first); assert.equal(receivers[1], second);
  assert.notEqual(first.tabBarList, second.tabBarList);
  assert.deepEqual(Array.from(first.tabBarList), ['initialized']);
  const descriptor = Object.getOwnPropertyDescriptor(first, 'tabBarList');
  const sentinel = {};
  Object.defineProperty(C.prototype, 'initTabBarList', {value() {throw sentinel;}, configurable: true});
  assert.throws(() => new C(), error => error === sentinel);
  return {events, keys: Object.keys(first), descriptor: {
    writable: descriptor.writable, enumerable: descriptor.enumerable, configurable: descriptor.configurable,
  }};
}
assert.deepEqual(homeObservation(actualHome), homeObservation(referenceHome));
