import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { SourceTextModule, SyntheticModule } from 'node:vm';

// Selected real decoded method; the oracle is test-only, never engine input.
assert.equal(createHash('sha256').update(readFileSync(process.argv[3])).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
// Repeated independent phase/tree proofs exceed the default work allowance for
// this 330-instruction method. No production default or memory limit is changed.
const work = 4_000_000;
const result = JSON.parse(execFileSync(process.argv[2],
  [process.argv[3], '8566', process.argv[4] ?? '--emit', '--work', String(work)], { encoding: 'utf8' }));
assert(result.fragment_complete);
assert.equal(result.record_complete, false);
assert.equal(result.work_limit, work);
const entry = new SourceTextModule(`${result.imports}\nexport default class { ${result.source} }`);
const windowModule = new SourceTextModule('let value; export { value as default }; export function set(x) { value = x; }');
const logModule = new SourceTextModule('let value; export { value as default }; export function set(x) { value = x; }');
await entry.link(name => {
  if (name === '@ohos:window') return windowModule;
  if (name === '@ohos:hilog') return logModule;
  assert(['@ohos:app.ability.AbilityConstant', '@ohos:display', '@ohos:sensor'].includes(name));
  return new SyntheticModule(['default'], function () { this.setExport('default', {}); });
});
await entry.evaluate();
const recovered = entry.namespace.default.prototype.updateWindowInfo;
function expected(receiver, api, log) {
  try {
    receiver.mainWindowInfo.windowStatusType = receiver.mainWindow.getWindowStatus();
    const width = receiver.mainWindow.getWindowProperties().windowRect.width;
    const height = receiver.mainWindow.getWindowProperties().windowRect.height;
    const size = { width: null, height: null, ['width']: width, ['height']: height };
    receiver.mainWindowInfo.windowSize = size;
    receiver.mainWindowInfo.widthBp = receiver.uiContext.getWindowWidthBreakpoint();
    receiver.mainWindowInfo.heightBp = receiver.uiContext.getWindowHeightBreakpoint();
    receiver.mainWindow.on('windowSizeChange', receiver.onWindowSizeChange);
    receiver.mainWindowInfo.avoidSystem = receiver.mainWindow.getWindowAvoidArea(api().AvoidAreaType.TYPE_SYSTEM);
    receiver.mainWindowInfo.avoidNavigationIndicator = receiver.mainWindow.getWindowAvoidArea(api().AvoidAreaType.TYPE_NAVIGATION_INDICATOR);
    receiver.mainWindowInfo.avoidCutout = receiver.mainWindow.getWindowAvoidArea(api().AvoidAreaType.TYPE_CUTOUT);
    receiver.mainWindowInfo.avoidSystemGesture = receiver.mainWindow.getWindowAvoidArea(api().AvoidAreaType.TYPE_SYSTEM_GESTURE);
    receiver.mainWindowInfo.avoidKeyboard = receiver.mainWindow.getWindowAvoidArea(api().AvoidAreaType.TYPE_KEYBOARD);
    receiver.mainWindow.on('avoidAreaChange', receiver.onAvoidAreaChange);
    receiver.mainWindowInfo.titleButtonRect = receiver.mainWindow.getTitleButtonRect();
    receiver.mainWindow.on('windowTitleButtonRectChange', receiver.onWindowButtonRectChange);
  } catch (error) {
    log.error(0, 'TestLog', 'Failed to update window info. Code: ' + error.code + ', message: ' + error.message + '');
  }
}
function invoke(fn, failAt, thrown, rebind) {
  const effects = [];
  const mark = (...event) => {
    effects.push(event);
    if (effects.length === failAt) throw thrown;
  };
  const watched = (name, target) => new Proxy(target, {
    get(object, key, receiver) { mark('get', name, key); return Reflect.get(object, key, receiver); },
  });
  const info = new Proxy({}, {
    set(target, key, value) { mark('set', 'info', key, value); target[key] = value; return true; },
  });
  const rect = watched('rect', { width: 640, height: 480 });
  const properties = watched('properties', { windowRect: rect });
  const areas = ['TYPE_SYSTEM', 'TYPE_NAVIGATION_INDICATOR', 'TYPE_CUTOUT', 'TYPE_SYSTEM_GESTURE', 'TYPE_KEYBOARD'];
  const api = version => watched('window-api', {
    AvoidAreaType: watched('area-types', Object.fromEntries(areas.map((key, index) => [key, version + index]))),
  });
  let currentApi = api(0);
  windowModule.namespace.set(currentApi);
  const functions = {
    getWindowStatus: () => 2,
    getWindowProperties: () => properties,
    getWindowWidthBreakpoint: () => 'wide',
    getWindowHeightBreakpoint: () => 'tall',
    getWindowAvoidArea: value => value,
    getTitleButtonRect: () => 42,
    on: () => undefined,
  };
  let window;
  window = new Proxy({}, { get(target, key) {
    mark('get', 'window', key);
    if (rebind && key === 'getWindowAvoidArea') {
      currentApi = api(100); windowModule.namespace.set(currentApi);
    }
    assert(Object.hasOwn(functions, key));
    return new Proxy(function () {}, {
      get() { assert.fail('no call/apply lookup'); },
      apply(target, receiver, args) {
        assert.equal(receiver, window); mark('call', key, ...args); return functions[key](...args);
      },
    });
  } });
  const receiver = watched('this', {
    mainWindowInfo: info, mainWindow: window, uiContext: window,
    onWindowSizeChange: 1, onAvoidAreaChange: 2, onWindowButtonRectChange: 3,
  });
  const log = watched('log', { error(...args) { assert.equal(this, log); mark('log', ...args); } });
  logModule.namespace.set(log);
  try { return { effects, returned: fn(receiver, () => currentApi, log) }; }
  catch (error) { return { effects, thrown: error }; }
}
const arbitrary = [undefined, null, false, -0, NaN, 7n, Symbol('thrown'), { code: 9, message: 'fault' }];
const normal = invoke(expected, 0, undefined, false);
assert(!('thrown' in normal));
for (const rebind of [false, true]) {
  for (const thrown of arbitrary) {
    // Inject failure at every normal-path observable operation, plus success.
    for (let at = 0; at <= normal.effects.length; ++at) {
      const oracle = invoke(expected, at, thrown, rebind);
      const actual = invoke(receiver => Reflect.apply(recovered, receiver, []), at, thrown, rebind);
      assert.deepEqual(actual.effects, oracle.effects);
      assert.equal('thrown' in actual, 'thrown' in oracle);
      if ('thrown' in oracle) {
        if (Object.is(oracle.thrown, thrown)) assert(Object.is(actual.thrown, thrown));
        else assert.equal(actual.thrown.constructor, oracle.thrown.constructor);
      } else assert(Object.is(actual.returned, oracle.returned));
    }
  }
}
const map = result.positions;
assert.equal(map.operations.length, 330);
const originals = map.origins.filter(origin => origin.kind === 'original');
assert.equal(originals.length, 330);
const bytes = Buffer.from(result.source);
function pcs(id) {
  const origin = map.origins[id];
  if ('pc' in origin) return [origin.pc];
  assert.equal(origin.kind, 'combined');
  return map.origin_parts.slice(origin.first_part, origin.first_part + origin.part_count).flatMap(pcs);
}
for (const placement of map.operations) {
  const span = placement.kind === 'anchor' ? map.anchors[placement.node] :
    placement.kind === 'property' ? map.properties[placement.node] :
    placement.kind === 'statement' ? map.statements[placement.node] : map.expressions[placement.node];
  assert(pcs(span.origin).includes(originals[placement.operation].pc));
  if (placement.kind === 'anchor') assert(span.byte <= bytes.length);
  else assert(span.begin < span.end && span.end <= bytes.length);
}
for (const pc of [331, 337, 339, 363]) assert(originals.some(origin => origin.pc === pc));
