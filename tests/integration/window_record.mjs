import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, abc, paired] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
// Finish ABC-only generation before reading the independent paired module.
const report = JSON.parse(execFileSync(tool, [abc, '8431', '--record-source',
  '--value-source', '--language-numeric'], {encoding: 'utf8', timeout: 60_000}));
assert.equal(report.record, 7618); assert.equal(report.source_complete, true);
assert.equal(report.route, 'value_tree'); assert.equal(report.legacy_writer_used, false);
assert.equal(report.executable, false);
assert.doesNotMatch(report.source, /INCOMPLETE|UNRECOVERED|Reflect\.|\bswitch\s*\(/);
for (const pattern of [/new WindowInfo\(/, /async \(windowSize\)/, /sensor\.on\(/,
  /\.then\(/, /getCalDegree\(x, y, z\)/, /-x/, /export \{ WindowInfo/])
  assert.match(report.source, pattern);
const pairedText = readFileSync(paired, 'utf8');
assert.equal((pairedText.match(/^@Observed$/gm) ?? []).length, 1);
// Uninitialized TS type declarations have no pinned assignment effect. Node
// retains native fields, so erase only these declarations in the oracle. All
// initialized fields, callback bodies, methods, enums and module code remain.
const declarations = /^    public [A-Za-z_$][\w$]*\??: [^\n;=]+;\n/gm;
assert.equal((pairedText.match(declarations) ?? []).length, 8);
const reference = stripTypeScriptTypes(pairedText.replace(declarations, '')
  .replace(/^@Observed$/gm, ''), {mode: 'transform'});
const methods = [
  ['updateAvoidPadding', 1], ['setImmersiveType', 1], ['setUIContext', 0],
  ['setSystemBarEnabled', 1], ['recover', 0], ['setWindowOrientation', 1],
  ['updateWindowInfo', 0], ['startWindowStatusListener', 0], ['release', 0],
  ['setSplitScreen', 3], ['cancelSplitScreen', 0], ['setWindowLimits', 4],
  ['resize', 2], ['queryOrientationByResourceManager', 0], ['queryDegree', 0],
  ['getCalDegree', 3],
];
const fields = ['onStatusTypeChange', 'onWindowButtonRectChange', 'onWindowSizeChange', 'onAvoidAreaChange'];
const initializedInfo = ['windowStatusType', 'isImmersive', 'orientation', 'windowSize',
  'widthBp', 'heightBp', 'statusBarMargin', 'avoidAreaPadding'];

async function load(source, generated = false) {
  let active;
  const token = {}, abort = {}, value = {};
  const identities = new WeakMap([[token, 'failure'], [abort, 'abort'], [value, 'result']]);
  const normalize = input => {
    if (input === null || typeof input !== 'object' && typeof input !== 'function') return input;
    if (identities.has(input)) return identities.get(input);
    if (typeof input === 'function') return ['function', input.length, Object.hasOwn(input, 'prototype')];
    return Reflect.ownKeys(input).map(key => {
      const d = Object.getOwnPropertyDescriptor(input, key);
      assert.ok('value' in d, `unexpected unobserved accessor ${String(key)}`);
      return [key, normalize(d.value), d.writable, d.enumerable, d.configurable];
    });
  };
  const dependencies = new Map();
  const event = (name, ...args) => {
    if (!active) return;
    active.trace.push([name, ...args.map(normalize)]);
    const step = ++active.steps;
    if (step === active.swapAt) {
      dependencies.get('@ohos:window').setExport('default', windows[1]);
      if (active.owner) { active.owner.mainWindow = mainWindows[1]; active.owner.uiContext = uis[1]; }
      active.trace.push(['swap']);
    }
    if (step === active.failAt) throw token;
  };
  Object.defineProperties(token, {
    code: {get() { event('error.code'); if (active.errorGetter === 'code') throw abort; return 23; }},
    message: {get() { event('error.message'); if (active.errorGetter === 'message') throw abort; return 'failure'; }},
  });
  const surface = (label, values) => {
    let object;
    object = new Proxy({}, {get(_target, key, receiver) {
      assert.equal(receiver, object); assert.ok(Object.hasOwn(values, key), `${label}.${String(key)}`);
      event(`${label}.get`, key);
      const member = values[key];
      if (typeof member !== 'function') return member;
      const fn = function(...args) {
        assert.equal(this, object); event(`${label}.call`, key, ...args); return member(...args);
      };
      Object.defineProperty(fn, 'call', {get() { assert.fail('ambient call adapter'); }});
      return fn;
    }});
    identities.set(object, label); return object;
  };
  const enumValues = (label, entries) => surface(label, entries);
  const windows = [0, 1].map(index => surface(`window${index}`, {
    WindowStatusType: enumValues(`status${index}`, {UNDEFINED: -1, MAXIMIZE: 1 + index * 10,
      FLOATING: 2 + index * 10, FULL_SCREEN: 3 + index * 10}),
    Orientation: enumValues(`orientation${index}`, {UNSPECIFIED: 0}),
    AvoidAreaType: enumValues(`avoid${index}`, {TYPE_SYSTEM: 0, TYPE_CUTOUT: 1,
      TYPE_SYSTEM_GESTURE: 2, TYPE_KEYBOARD: 3, TYPE_NAVIGATION_INDICATOR: 4}),
  }));
  function monitor(promise, request) {
    request.owned.push(promise); promise.then(() => {}, () => {});
    const wrapper = {};
    for (const name of ['then', 'catch']) Object.defineProperty(wrapper, name, {get() {
      event('promise.get', request.id, name);
      return function(callback) {
        assert.equal(this, wrapper); event('promise.call', request.id, name, callback);
        return monitor(Promise.prototype[name].call(promise, callback), request);
      };
    }});
    identities.set(wrapper, 'promise'); return wrapper;
  }
  function request() {
    const entry = {id: active.requests.length, owned: []};
    active.requests.push(entry);
    return monitor(new Promise((resolve, reject) => {entry.resolve = resolve; entry.reject = reject;}), entry);
  }
  const resource = surface('resource', {getConfigurationSync: () => ({direction: 7})});
  const hosts = [0, 1].map(index => surface(`host${index}`, {
    startAbility: () => request(), terminateSelf: () => request(), resourceManager: resource,
  }));
  const uis = [0, 1].map(index => surface(`ui${index}`, {
    px2vp: input => input / 2, getWindowWidthBreakpoint: () => 'medium',
    getWindowHeightBreakpoint: () => 'large', getHostContext: () =>
      active.uiMode === 'host-null' ? null : active.uiMode === 'host-undefined' ? undefined : hosts[index],
  }));
  const title = {}; identities.set(title, 'title');
  const rectangles = ['topRect', 'leftRect', 'rightRect', 'bottomRect'];
  const area = {};
  for (const [index, name] of rectangles.entries()) {
    const rect = {};
    for (const key of ['height', 'width', 'top', 'left']) Object.defineProperty(rect, key, {get() {
      event('rectangle.value', name, key);
      return key === 'height' || key === 'width' ? active.mask & (1 << index) ? 10 : 0 : 2;
    }});
    Object.defineProperty(area, name, {get() { event('rectangle', name); return rect; }});
  }
  identities.set(area, 'area');
  const mainWindows = [0, 1].map(index => surface(`main${index}`, {
    getUIContext: () => active.uiMode === 'null' ? null : active.uiMode === 'undefined' ? undefined : uis[index],
    getWindowStatus: () => active.status,
    getWindowProperties: () => ({windowRect: {width: ++active.properties, height: ++active.properties}}),
    getWindowAvoidArea: () => area, getTitleButtonRect: () => title,
    on: (name, callback) => {active.listeners.push({name, callback});},
    off: () => undefined, setWindowDecorVisible: () => undefined,
    setWindowDecorHeight: () => undefined, setDecorButtonStyle: () => undefined,
    setWindowSystemBarEnable: () => request(), recover: () => request(),
    setPreferredOrientation: () => request(), setWindowLimits: () => request(),
    resize: (_width, _height, callback) => { active.resize.push(callback); },
  }));
  const sensor = surface('sensor', {
    SensorId: enumValues('sensorId', {GRAVITY: 9}),
    on: (_id, callback) => { active.gravity.push(callback); },
  });
  const display = surface('display', {getDefaultDisplaySync: () => ({width: 700, height: 900})});
  const log = surface('log', {info: () => undefined, error: () => undefined});
  const context = vm.createContext({
    Observed: Type => Type,
    WidthBreakpoint: enumValues('widthBp', {WIDTH_XS: 'small'}),
    HeightBreakpoint: enumValues('heightBp', {HEIGHT_SM: 'short'}),
    JSON: surface('JSON', {stringify: input => JSON.stringify(input)}),
  });
  const imports = new Map([
    ['@ohos:window', windows[0]], ['@ohos:display', display], ['@ohos:hilog', log], ['@ohos:sensor', sensor],
    ['@ohos:app.ability.AbilityConstant', surface('ability', {WindowMode:
      enumValues('windowMode', {WINDOW_MODE_SPLIT_PRIMARY: 8})})],
  ]);
  const module = new vm.SourceTextModule(source, {context});
  await module.link(specifier => {
    assert.ok(imports.has(specifier), specifier);
    const dependency = new vm.SyntheticModule(['default'], function() {
      this.setExport('default', imports.get(specifier));
    }, {context});
    dependencies.set(specifier, dependency); return dependency;
  });
  assert.throws(() => module.namespace.WindowUtil, {name: 'ReferenceError'});
  await module.evaluate();
  assert.deepEqual(Object.getOwnPropertyNames(module.namespace), ['ImmersiveType', 'WindowInfo', 'WindowUtil']);
  const {WindowUtil: Type, WindowInfo: Info, ImmersiveType: modes} = module.namespace;
  const NativePromise = vm.runInContext('Promise', context);
  assert.equal(Type.name, 'WindowUtil'); assert.equal(Type.length, 1);
  assert.equal(Info.name, 'WindowInfo'); assert.equal(Info.length, 0);
  assert.deepEqual(normalize(modes), [['0', 'NORMAL', true, true, true], ['1', 'IMMERSIVE', true, true, true],
    ['NORMAL', 0, true, true, true], ['IMMERSIVE', 1, true, true, true]]);
  for (const [name, length] of methods) {
    const d = Object.getOwnPropertyDescriptor(Type.prototype, name);
    assert.equal(d.value.name, name); assert.equal(d.value.length, length);
    assert.equal(d.writable, true); assert.equal(d.enumerable, false); assert.equal(d.configurable, true);
  }
  return {assignments() {
    assert.equal(generated, true);
    let cases = 0;
    // STOBJBYNAME is assignment, including inherited setter dispatch. Native
    // retained TS field definitions are deliberately not this semantic oracle.
    for (const [Constructor, names, args] of [[Info, initializedInfo, []],
      [Type, ['mainWindowInfo', ...fields, 'mainWindow'], [mainWindows[0]]]])
      for (const name of names) for (const fails of [false, true]) for (const redirected of [false, true]) {
        dependencies.get('@ohos:window').setExport('default', windows[0]);
        active = {trace: [], steps: 0, failAt: 0, swapAt: 0};
        let writes = 0, receiver, saved, prototypeReads = 0;
        const alternate = Object.create(Constructor.prototype);
        const target = new Proxy(function() {}, {get(object, key, proxy) {
          if (key === 'prototype') { ++prototypeReads; return alternate; }
          return Reflect.get(object, key, proxy);
        }});
        Object.defineProperty(Constructor.prototype, name, {configurable: true, set(input) {
          ++writes; receiver = this; saved = input;
          assert.equal(Object.getPrototypeOf(this), redirected ? alternate : Constructor.prototype);
          if (fails) throw token;
          Object.defineProperty(this, name, {value: input, writable: true, enumerable: true, configurable: true});
        }});
        try {
          const construct = () => Reflect.construct(Constructor, args, redirected ? target : Constructor);
          if (fails) assert.throws(construct, error => error === token);
          else {
            const instance = construct(); assert.equal(instance, receiver); assert.equal(instance[name], saved);
            if (name === 'mainWindow') assert.equal(saved, mainWindows[0]);
            const d = Object.getOwnPropertyDescriptor(instance, name);
            assert.equal(d.writable, true); assert.equal(d.enumerable, true); assert.equal(d.configurable, true);
          }
          assert.equal(writes, 1); assert.equal(prototypeReads, redirected ? 1 : 0); ++cases;
        } finally { delete Constructor.prototype[name]; }
      }
    return cases;
  }, async observe(scenario) {
    dependencies.get('@ohos:window').setExport('default', windows[0]);
    active = {trace: [], steps: 0, properties: 0, requests: [], listeners: [], resize: [], gravity: [],
      failAt: 0, swapAt: 0, errorGetter: '', status: 3, mask: 15, uiMode: 'live', ...scenario};
    const outcomes = [];
    const invoke = async (fn, receiver, args) => {
      let returned = false;
      try {
        const result = Reflect.apply(fn, receiver, args); returned = true;
        active.trace.push(['invoke.return', result instanceof NativePromise ? 'promise' : 'value']);
        outcomes.push(['return', normalize(await result)]);
      }
      catch (error) {
        const completion = returned ? 'rejection' : 'throw';
        if (error === token || error === abort) outcomes.push([completion, normalize(error)]);
        else { assert.equal(error.name, 'TypeError'); outcomes.push([completion, 'TypeError']); }
      }
    };
    let owner;
    try {
      owner = new Type(mainWindows[0]); active.owner = owner; identities.set(owner, 'owner');
      assert.equal(owner.mainWindow, mainWindows[0]);
      assert.equal(Object.getPrototypeOf(owner.mainWindowInfo), Info.prototype);
      assert.deepEqual(Reflect.ownKeys(owner.mainWindowInfo), initializedInfo);
      for (const name of fields) {
        assert.equal(owner[name].length, 1); assert.equal(Object.hasOwn(owner[name], 'prototype'), false);
        // Constructor assignment retains pinned empty callback names; native
        // TS fields infer property names. That lowering difference is explicit.
        assert.equal(owner[name].name, generated ? '' : name);
        const d = Object.getOwnPropertyDescriptor(owner, name);
        assert.equal(d.writable, true); assert.equal(d.enumerable, true); assert.equal(d.configurable, true);
      }
      if (scenario.method !== 'setUIContext') owner.uiContext = active.uiMode === 'null' ? null :
        active.uiMode === 'undefined' ? undefined : uis[0];
      const args = {
        updateAvoidPadding: [area], setImmersiveType: [active.input], setSystemBarEnabled: [active.input],
        setWindowOrientation: [active.input], setSplitScreen: ['bundle', 'ability', 'module'],
        setWindowLimits: [800, 900, 200, 300], resize: [17, 19], getCalDegree: [1, 2, 0],
        onStatusTypeChange: [active.input], onWindowButtonRectChange: [title], onWindowSizeChange: [value],
        onAvoidAreaChange: [{get type() {event('avoidOptions.type'); return active.input;},
          get area() {event('avoidOptions.area'); return area;}}],
      };
      if (scenario.method === 'concurrent') {
        await invoke(owner.setWindowOrientation, owner, [17]);
        await invoke(owner.setWindowOrientation, owner, [19]);
        await invoke(owner.setSystemBarEnabled, owner, [true]);
        await invoke(owner.setWindowLimits, owner, [800, 900, 200, 300]);
        // The requests are still pending. Replace the instance's main window,
        // then construct a second owner before the saved callbacks execute.
        owner.mainWindow = mainWindows[1];
        const other = new Type(mainWindows[1]); identities.set(other, 'other');
        assert.notEqual(other.onWindowSizeChange, owner.onWindowSizeChange);
        await invoke(other.setWindowOrientation, other, [23]);
        active.other = other;
      } else if (scenario.method === 'lifecycle') {
        for (const name of ['setUIContext', 'updateWindowInfo', 'startWindowStatusListener'])
          await invoke(owner[name], owner, []);
        const saved = [...active.listeners];
        if (scenario.otherInstance) {
          const other = new Type(mainWindows[1]);
          assert.notEqual(other.mainWindowInfo, owner.mainWindowInfo);
          assert.notEqual(other.mainWindowInfo.windowSize, owner.mainWindowInfo.windowSize);
          assert.notEqual(other.mainWindowInfo.avoidAreaPadding, owner.mainWindowInfo.avoidAreaPadding);
          for (const name of fields) assert.notEqual(other[name], owner[name]);
        }
        for (const {name, callback} of saved) {
          const argument = name === 'windowSizeChange' ? value : name === 'avoidAreaChange' ? args.onAvoidAreaChange[0] :
            name === 'windowTitleButtonRectChange' ? title : active.input;
          await invoke(callback, {unrelated: true}, [argument]);
        }
        await invoke(owner.release, owner, []);
      } else await invoke(owner[scenario.method], fields.includes(scenario.method) ? {unrelated: true} : owner,
        args[scenario.method] ?? []);
      for (const callback of active.resize) await invoke(callback, {unrelated: true},
        [scenario.reject ? token : {get code() {event('resize.code'); return 0;}}]);
      for (const callback of active.gravity) await invoke(callback, {unrelated: true}, [{
        get x() {event('gravity.x'); return 1;}, get y() {event('gravity.y'); return 2;},
        get z() {event('gravity.z'); return 0;},
      }]);
    } catch (error) {
      if (error === token || error === abort) outcomes.push(['construct', normalize(error)]);
      else throw error;
    }
    const requests = scenario.reverse ? [...active.requests].reverse() : active.requests;
    for (const entry of requests) {
      active.trace.push(['settle', entry.id, !!scenario.reject]);
      if (scenario.reject) entry.reject(token); else entry.resolve(value);
    }
    const settled = await Promise.allSettled(active.requests.flatMap(entry => entry.owned));
    if (scenario.method === 'concurrent' && !scenario.failAt && !scenario.reject && !scenario.swapAt) {
      assert.equal(owner.mainWindowInfo.orientation, scenario.reverse ? 17 : 19);
      assert.equal(active.other.mainWindowInfo.orientation, 23);
    }
    const state = owner ? normalize(owner.mainWindowInfo) : undefined;
    return {trace: active.trace, outcomes, state, own: owner ? Reflect.ownKeys(owner) : undefined,
      other: active.other ? normalize(active.other.mainWindowInfo) : undefined,
      settled: settled.map(item => item.status === 'fulfilled' ? ['return', normalize(item.value)] :
        item.reason === token || item.reason === abort ? ['throw', normalize(item.reason)] : ['throw', item.reason.name])};
  }};
}

const actual = await load(report.source, true), expected = await load(reference);
const assignmentScenarios = actual.assignments();
let scenarios = 0;
async function compare(scenario) {
  assert.deepEqual(await actual.observe(scenario), await expected.observe(scenario), JSON.stringify(scenario));
  ++scenarios;
}
for (const method of [...methods.map(([name]) => name), ...fields, 'lifecycle', 'concurrent'])
  for (const reject of [false, true]) for (const reverse of [false, true])
    for (const input of [undefined, null, false, 0, 1, 2, 3, 4, 'unknown'])
      await compare({method, reject, reverse, input, otherInstance: method === 'lifecycle'});
for (const method of [...methods.map(([name]) => name), ...fields, 'lifecycle', 'concurrent'])
  for (const uiMode of ['live', 'null', 'undefined', 'host-null', 'host-undefined'])
    for (let failAt = 1; failAt <= 55; ++failAt) await compare({method, uiMode, failAt, input: 1});
for (const method of ['lifecycle', 'updateWindowInfo', 'onAvoidAreaChange', 'setWindowOrientation', 'setImmersiveType'])
  for (let swapAt = 1; swapAt <= 40; ++swapAt) await compare({method, swapAt, input: 1});
for (const method of [...methods.map(([name]) => name), ...fields])
  for (const errorGetter of ['code', 'message']) for (const reject of [false, true])
    for (const failAt of [0, 6, 9, 12, 18]) await compare({method, errorGetter, reject, failAt, input: 1});
let mutationChecks = 0;
for (const [from, to, scenario] of [
  ['this.mainWindow.on("windowSizeChange", this.onWindowSizeChange);', '', {method: 'lifecycle', input: 1}],
  ['windowSize = windowSize', 'windowSize = void 0', {method: 'onWindowSizeChange'}],
  ['.mainWindowInfo.orientation = orientation', '.mainWindowInfo.orientation = void 0', {method: 'setWindowOrientation', input: 2}],
  ['sensor.on(sensor.SensorId.GRAVITY', 'sensor.on(void 0', {method: 'queryDegree'}],
]) {
  assert.ok(report.source.includes(from), from);
  const changed = report.source.replace(from, to);
  new vm.SourceTextModule(changed); // Syntax failure is not semantic detection.
  const mutant = await load(changed, true);
  await assert.rejects(async () => assert.deepEqual(await mutant.observe(scenario), await expected.observe(scenario)),
    error => error instanceof assert.AssertionError);
  ++mutationChecks;
}
const fieldWrite = '(this.windowStatusType = window.WindowStatusType.UNDEFINED);';
assert.ok(report.source.includes(fieldWrite));
const ownDefinition = report.source.replace(fieldWrite,
  'Object.defineProperty(this, "windowStatusType", {value: window.WindowStatusType.UNDEFINED, writable: true, enumerable: true, configurable: true});');
new vm.SourceTextModule(ownDefinition);
const fieldMutant = await load(ownDefinition, true);
assert.throws(() => fieldMutant.assignments(), error => error instanceof assert.AssertionError);
++mutationChecks;
console.log(JSON.stringify({wholeRecordBehavior: true, pairedScenarios: scenarios, mutationChecks,
  assignmentScenarios, prototypeMethods: methods.length, callbackFields: fields.length, platformExecution: false}));
