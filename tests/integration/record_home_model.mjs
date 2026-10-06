import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [sourcePath, pairedPath] = process.argv.slice(2);
const source = await readFile(sourcePath, 'utf8');
// Generation has already completed using only ABC; this is an independent oracle.
const paired = stripTypeScriptTypes(await readFile(pairedPath, 'utf8'));
const specifier = '@normalized:N&&&shopping/src/main/ets/constants/HomeConstants&1.0.0';
assert.match(source, /constructor\(\)/);
assert.match(source, /this\.tabBarList\s*=\s*\[\]/);
assert.match(source, /\.map\(/);
assert.match(source, /\.push\(/);
assert.doesNotMatch(source, /INCOMPLETE|UNRECOVERED|\.call\(|Reflect\.construct/);

async function load(text, constants) {
  const context = vm.createContext({});
  const dependency = new vm.SyntheticModule(['HomeConstants'], function() {
    this.setExport('HomeConstants', constants);
  }, {context});
  const module = new vm.SourceTextModule(text, {context});
  await module.link(name => { assert.equal(name, specifier); return dependency; });
  assert.throws(() => module.namespace.HomeModel, {name: 'ReferenceError'});
  await module.evaluate();
  return {C: module.namespace.HomeModel, dependency};
}
const snapshot = list => Array.from(list, item => Reflect.ownKeys(item).map(key => {
  const descriptor = Object.getOwnPropertyDescriptor(item, key);
  return [key, descriptor.value, descriptor.writable, descriptor.enumerable, descriptor.configurable];
}));

async function observe(text, count, mode, throwAt) {
  const trace = [], sentinel = {}, retained = [];
  let current, captured, reads = 0, dependency;
  const event = name => { trace.push(name); if (throwAt === name) throw sentinel; };
  const texts = Array.from({length: count}, (_, i) => `text:${i}`);
  const constants = {
    get TAB_BAR_TEXT_LIST() {
      event('text');
      if (mode === 'live') dependency.setExport('HomeConstants', {
        TAB_BAR_TEXT_LIST: texts,
        get TAB_BAR_ICON_LIST() { event('live-icon'); return ['new-icon']; },
        get TAB_BAR_ACTIVE_ICON_LIST() { event('live-active'); return ['new-active']; },
      });
      return texts;
    },
    get TAB_BAR_ICON_LIST() { event('icon'); return Array.from({length: count}, (_, i) => `icon:${i}`); },
    get TAB_BAR_ACTIVE_ICON_LIST() { event('active'); return Array.from({length: count}, (_, i) => `active:${i}`); },
  };
  Object.defineProperty(texts, 'map', {get() {
    event('map-get');
    return function(callback) {
      assert.equal(this, texts); event('map-call');
      const result = Array.prototype.map.call(texts, callback);
      assert.ok(result.every(item => item === undefined));
      return result;
    };
  }});
  const loaded = await load(text, constants); dependency = loaded.dependency;
  const C = loaded.C, init = C.prototype.initTabBarList;
  assert.equal(C.name, 'HomeModel'); assert.equal(C.length, 0);
  assert.equal(init.length, 0); assert.equal(C.prototype.getTabBarList.length, 0);
  assert.throws(() => C(), {name: 'TypeError'});
  function track(list) {
    retained.push(list);
    Object.defineProperty(list, 'push', {configurable: true, get() {
      event('push-get');
      if (mode === 'replace-list' && ++reads === 1) captured.tabBarList = track([]);
      const fn = function(item) {
        assert.equal(this, list); event('push-call');
        return Array.prototype.push.call(list, item);
      };
      // An emitter must invoke the acquired callee with its receiver directly.
      Object.defineProperty(fn, 'call', {get() {throw new Error('function.call read');}});
      return fn;
    }});
    return list;
  }
  Object.defineProperty(C.prototype, 'initTabBarList', {configurable: true, get() {
    captured = this; current = track(this.tabBarList); event('init-get');
    const fn = function() { assert.equal(this, captured); event('init-call'); return Reflect.apply(init, this, []); };
    Object.defineProperty(fn, 'call', {get() {throw new Error('init.call read');}});
    return fn;
  }});
  let thrown = false;
  try { new C(); } catch (error) { assert.equal(error, sentinel); thrown = true; }
  assert.equal(thrown, throwAt !== null);
  assert.ok(captured);
  const descriptor = Object.getOwnPropertyDescriptor(captured, 'tabBarList');
  assert.deepEqual([descriptor.writable, descriptor.enumerable, descriptor.configurable], [true, true, true]);
  assert.equal(captured.getTabBarList(), captured.tabBarList);
  if (!thrown && mode === 'ordinary') {
    const first = captured.tabBarList;
    Reflect.apply(init, captured, []);
    assert.equal(captured.getTabBarList(), first);
    assert.equal(first.length, count * 2);
    const prior = captured;
    new C();
    assert.notEqual(captured.tabBarList, prior.tabBarList);
  }
  return {trace, lists: retained.map(snapshot), current: snapshot(current), fields: Object.keys(captured), thrown};
}

let pairedCases = 0;
for (const count of [0, 1, 2, 200, 201]) for (const mode of ['ordinary', 'live', 'replace-list']) {
  const points = [null, 'init-get', 'init-call', 'text', 'map-get', 'map-call'];
  if (count) points.push('push-get', 'icon', 'active', 'push-call');
  if (mode === 'live') {
    const absent = new Set(['icon', 'active']);
    for (let i = points.length - 1; i >= 0; --i) if (absent.has(points[i])) points.splice(i, 1);
    if (count) points.push('live-icon', 'live-active');
  }
  for (const point of points) {
    assert.deepEqual(await observe(source, count, mode, point), await observe(paired, count, mode, point));
    ++pairedCases;
  }
}

// The pinned STOBJBYNAME is assignment, unlike Node's retained native TS field
// declarations. Test this obligation separately against explicit observable
// expectations; the paired checks above do not certify inherited-setter rules.
async function assignment(text, abrupt, alternate) {
  const {C} = await load(text, {});
  const writes = [], trace = [], sentinel = {};
  const prototype = alternate ? {} : C.prototype;
  let receiver;
  Object.defineProperty(prototype, 'tabBarList', {set(value) {
    receiver = this; writes.push(value); trace.push('set');
    assert.ok(Array.isArray(value)); assert.equal(value.length, 0);
    if (abrupt) throw sentinel;
  }});
  Object.defineProperty(prototype, 'initTabBarList', {get() {
    assert.equal(this, receiver); trace.push('init-get');
    return function() { assert.equal(this, receiver); trace.push('init-call'); };
  }});
  function Target() {} Target.prototype = prototype;
  if (abrupt) assert.throws(() => Reflect.construct(C, [], alternate ? Target : C), error => error === sentinel);
  else {
    const first = Reflect.construct(C, [], alternate ? Target : C);
    assert.equal(first, receiver); assert.equal(Object.getPrototypeOf(first), prototype);
    assert.equal(Object.hasOwn(first, 'tabBarList'), false);
    const second = Reflect.construct(C, [], alternate ? Target : C);
    assert.notEqual(first, second); assert.notEqual(writes[0], writes[1]);
  }
  assert.deepEqual(trace, abrupt ? ['set'] : ['set', 'init-get', 'init-call', 'set', 'init-get', 'init-call']);
}
for (const abrupt of [false, true]) for (const alternate of [false, true]) await assignment(source, abrupt, alternate);

const mutations = [
  {name: 'assignment replaced by own definition',
   changed: source.replace('this.tabBarList = []', 'Object.defineProperty(this, "tabBarList", {value: [], writable: true, enumerable: true, configurable: true})'),
   check: text => assignment(text, false, false), compare: false},
  {name: 'constructor initialization omitted', changed: source.replace('this.initTabBarList();', 'void 0;'),
   check: text => observe(text, 2, 'ordinary', null), compare: true},
  {name: 'object property renamed', changed: source.replace('activeIcon:', 'wrongIcon:'),
   check: text => observe(text, 2, 'ordinary', null), compare: true},
];
for (const {name, changed, check, compare} of mutations) {
  assert.notEqual(changed, source);
  new vm.SourceTextModule(changed); // Invalid syntax cannot count as detection.
  let detected = false;
  try {
    if (compare) assert.deepEqual(await check(changed), await check(source));
    else await check(changed);
  } catch (error) { if (!(error instanceof assert.AssertionError)) throw error; detected = true; }
  assert.equal(detected, true, name);
}
console.log(`HomeModel whole-record: ${pairedCases} paired cases, 4 assignment cases, 3 mutations detected`);
