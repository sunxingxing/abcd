import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, abc, paired] = process.argv.slice(2);
const run = spawnSync(tool, [abc, '8202', '--record-source', '--value-source'], {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr);
const result = JSON.parse(run.stdout.trim());
assert.ok(result.source.includes('setImmersiveType(type)'));
assert.equal(result.record, 7618); assert.equal(result.source_complete, true);
assert.equal(result.executable, false); assert.equal(result.legacy_writer_used, undefined);
assert.doesNotMatch(result.source, /INCOMPLETE|UNRECOVERED/);
// The paired TS enters only this independent oracle. The decorator belongs to
// WindowInfo, which is not constructed or tested here; both modules load with
// identity Observed. The enum requires the standard TypeScript transform mode.
const pairedSource = readFileSync(paired, 'utf8').replace(/^@Observed\r?\n/m, '');
const classStart = pairedSource.indexOf('export class WindowUtil');
assert.ok(classStart > 0);
// Separate transforms retain runtime enum member reads. A single TypeScript
// transform folds constants, hiding getter/mutation behavior present in ABC.
const reference = stripTypeScriptTypes(pairedSource.slice(0, classStart), {mode: 'transform'}) +
  stripTypeScriptTypes(pairedSource.slice(classStart), {mode: 'transform'});
const values = [undefined, null, false, true, 0, -0, 1, NaN, '', 'text', 1n, Symbol('value'), {}, []];
async function scenario(source, method, value, failAt, reject) {
  const trace = []; let step = 0, stored, instance;
  const token = {get code() {event('code'); return 23;}, get message() {event('message'); return 'failure';}};
  function event(...parts) {trace.push(parts); if (++step === failAt) throw token;}
  const hilog = {get error() {
    event('get-log'); return function(...args) {assert.equal(this, hilog); event('log', ...args);};
  }};
  const request = {get catch() {
    event('get-catch'); return function(callback) {
      assert.equal(this, request); event('catch'); if (reject) callback(token);
    };
  }};
  const window = {get getUIContext() {
    event('get-context'); return function() {assert.equal(this, window); event('context'); return value;};
  }, get setWindowSystemBarEnable() {
    event('get-bars'); return function(bars) {assert.equal(this, window); event('bars', [...bars]); return request;};
  }};
  for (const name of ['setWindowDecorVisible', 'setWindowDecorHeight', 'setDecorButtonStyle'])
    Object.defineProperty(window, name, {get() {
      event('get', name); return function(argument) {
        assert.equal(this, window);
        event('call', name, name === 'setDecorButtonStyle' ? argument.buttonIconSize : argument);
      };
    }});
  const context = vm.createContext({Observed: C => C});
  const dependencies = new Map();
  const module = new vm.SourceTextModule(source, {context});
  await module.link(name => {
    assert.ok(['@ohos:app.ability.AbilityConstant', '@ohos:display', '@ohos:window', '@ohos:hilog', '@ohos:sensor'].includes(name), name);
    if (!dependencies.has(name)) dependencies.set(name, new vm.SyntheticModule(['default'], function() {
      this.setExport('default', name === '@ohos:hilog' ? hilog : {});
    }, {context}));
    return dependencies.get(name);
  });
  await module.evaluate();
  if (method === 'setImmersiveType') for (const name of ['NORMAL', 'IMMERSIVE']) {
    const initial = module.namespace.ImmersiveType[name];
    Object.defineProperty(module.namespace.ImmersiveType, name, {get() {event('enum', name); return initial;}});
  }
  // Only complete, restored prototype methods and their callbacks run. The
  // incomplete class constructor, omitted methods and full record acceptance
  // are outside this test, as are platform API implementations.
  instance = Object.create(module.namespace.WindowUtil.prototype);
  Object.defineProperty(instance, 'mainWindow', {get() {event('main-window'); return window;}});
  Object.defineProperty(instance, 'uiContext', {set(next) {event('set-context'); stored = next;}});
  const info = {set isImmersive(next) {event('set-immersive'); stored = next;}};
  Object.defineProperty(instance, 'mainWindowInfo', {get() {event('info'); return info;}});
  for (const name of ['recover', 'release']) Object.defineProperty(instance, name, {get() {
    event('get', name); return function() {assert.equal(this, instance); event('call', name);};
  }});
  let escaped;
  try {Reflect.apply(instance[method], instance, method === 'setUIContext' ? [] : [value]);} catch (error) {escaped = error;}
  if (escaped && escaped !== token) assert.fail('unexpected escaped exception');
  return {trace, escaped: escaped === token, stored};
}
let cases = 0;
for (const method of ['setUIContext', 'setSystemBarEnabled', 'setImmersiveType']) for (const value of values)
  for (const reject of [false, true]) for (let failAt = 0; failAt <= 16; ++failAt) {
    assert.deepEqual(await scenario(result.source, method, value, failAt, reject),
      await scenario(reference, method, value, failAt, reject)); ++cases;
  }
console.log(`${cases} paired WindowUtil restored method/callback scenarios passed; two omissions and incomplete record retained`);
