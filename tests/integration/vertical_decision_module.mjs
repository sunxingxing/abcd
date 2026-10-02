import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

// Compare the real getPaddingTop method under a bounded module/state contract.
// Constructors, storage and rendering are outside this paired-source check.
const [tool, root] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-vertical-decision-'));
const hostile = {[Symbol.toPrimitive]() { assert.fail('strict comparisons must not coerce'); }};
async function trace(source, breakpoint, deviceType, swap, fail, missing) {
  const events = [], failure = {}, returned = [], dependencies = new Map();
  const event = label => { events.push(label); if (label === fail) throw failure; };
  const context = vm.createContext({ViewPU: class {}});
  const prototype = vm.runInContext('Object.prototype', context);
  const device = {get deviceType() { event('device'); return deviceType; }};
  const oldDevice = {get deviceType() { assert.fail('module live binding was snapshotted'); }};
  let deviceModule;
  class OutsideMethod { constructor() { assert.fail('outside getPaddingTop'); } }
  const exports = {
    CommonConstants: {get DEVICE_2IN1() { event('common'); return '2in1'; }},
    BreakpointConstants: {get BREAKPOINT_LG() { event('breakpoint'); return 'lg'; }},
    MicroBlogView: OutsideMethod, CommentListView: OutsideMethod,
    CommentBarView: OutsideMethod, CardArrayViewModel: OutsideMethod
  };
  const module = new vm.SourceTextModule(source, {context});
  await module.link(specifier => {
    assert.ok(specifier === '@ohos:deviceInfo' || specifier.startsWith('@normalized:N&&&'));
    if (!dependencies.has(specifier)) {
      if (specifier === '@ohos:deviceInfo') {
        deviceModule = new vm.SyntheticModule(['default'], function() {
          this.setExport('default', missing ? undefined : swap ? oldDevice : device);
        }, {context}); dependencies.set(specifier, deviceModule);
      } else dependencies.set(specifier, new vm.SyntheticModule(Object.keys(exports), function() {
        for (const [key, value] of Object.entries(exports)) this.setExport(key, value);
      }, {context}));
    }
    return dependencies.get(specifier);
  });
  await module.evaluate();
  const instance = Object.create(module.namespace.DetailVertical.prototype);
  Object.defineProperty(instance, 'currentBreakpoint', {get() {
    event('state'); if (swap && !missing) deviceModule.setExport('default', device); return breakpoint;
  }});
  const values = [];
  for (let i = 0; i < 2; ++i) {
    try {
      const value = instance.getPaddingTop();
      assert.equal(Object.getPrototypeOf(value), prototype);
      assert.deepEqual(Object.keys(value), ['id', 'type', 'params', 'bundleName', 'moduleName']);
      for (const key of Object.keys(value)) {
        const descriptor = Object.getOwnPropertyDescriptor(value, key);
        assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
      }
      assert.ok(Array.isArray(value.params) && value.params.length === 0);
      if (returned.length) { assert.notEqual(value, returned[0]); assert.notEqual(value.params, returned[0].params); }
      returned.push(value); values.push(JSON.parse(JSON.stringify(value)));
    } catch (error) {
      if (error === failure) values.push('sentinel');
      else if (missing && error.name === 'TypeError') values.push('TypeError');
      else throw error;
    }
  }
  return {events, values};
}
try {
  const output = join(temporary, 'output');
  const run = spawnSync(tool, [join(root, 'modules.abc'), output, '--value-source'], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stderr);
  const record = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8')).records.find(value => value.entity === 44437);
  assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, false);
  const generated = readFileSync(join(output, record.source), 'utf8');
  const paired = stripTypeScriptTypes(readFileSync(join(root, 'src/picture/src/main/ets/view/DetailVerticalView.ts'), 'utf8'));
  let scenarios = 0;
  for (const breakpoint of ['sm', 'md', 'lg', undefined, Symbol('breakpoint'), hostile])
    for (const device of ['phone', '2in1', undefined, NaN, Symbol('device'), hostile])
      for (const swap of [false, true]) for (const fail of ['', 'state', 'breakpoint', 'device', 'common']) {
        assert.deepEqual(await trace(generated, breakpoint, device, swap, fail, false),
          await trace(paired, breakpoint, device, swap, fail, false));
        ++scenarios;
      }
  for (const breakpoint of ['sm', 'lg']) {
    assert.deepEqual(await trace(generated, breakpoint, undefined, false, '', true),
      await trace(paired, breakpoint, undefined, false, '', true));
    ++scenarios;
  }
  console.log(`Paired DetailVertical module decision: ${scenarios} bounded method scenarios passed`);
} finally { rmSync(temporary, {recursive: true, force: true}); }
