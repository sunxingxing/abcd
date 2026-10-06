import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
const [tool, abc, paired] = process.argv.slice(2);
// Generate from ABC alone before opening the independent reference. The
// incomplete class is never constructed; only these complete methods execute.
const run = spawnSync(tool, [abc, '8202', '--record-source', '--value-source', '--language-arithmetic'],
  {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr);
const report = JSON.parse(run.stdout);
assert.equal(report.record, 7618); assert.equal(report.source_complete, false);
assert.equal(report.executable, false); assert.equal(report.legacy_writer_used, false);
// Node does not parse ArkTS decorators. This fixture supplies an identity
// Observed decorator for the unused WindowInfo class in both modules; it does
// not test that host's observation protocol or construct either class.
const pairedText = readFileSync(paired, 'utf8');
assert.equal((pairedText.match(/^@Observed$/gm) ?? []).length, 1);
const reference = stripTypeScriptTypes(pairedText.replace(/^@Observed$/gm, ''), {mode: 'transform'});
const resultToken = Symbol('result');
async function load(source) {
  let current;
  const event = (...parts) => {
    current.trace.push(parts);
    if (++current.steps === current.failAt) throw current.failure;
  };
  const display = {get getDefaultDisplaySync() {
    assert.equal(this, display); event('get-display-method'); return function() {
      assert.equal(this, display); event('display-call');
      return {get width() {event('display-width'); return 700;}, get height() {event('display-height'); return 900;}};
    };
  }};
  const hilog = {get error() {event('get-log-method'); return function(...args) {
    assert.equal(this, hilog); event('log-error', ...args);
  };}};
  const context = vm.createContext({Observed: constructor => constructor});
  const module = new vm.SourceTextModule(source, {context});
  await module.link(name => new vm.SyntheticModule(['default'], function() {
    this.setExport('default', name === '@ohos:display' ? display : name === '@ohos:hilog' ? hilog :
      name === '@ohos:app.ability.AbilityConstant' ? {get WindowMode() {
        event('window-mode'); return {get WINDOW_MODE_SPLIT_PRIMARY() {event('split-mode'); return 19;}};
      }} : {});
  }, {context}));
  await module.evaluate();
  const C = module.namespace.WindowUtil;
  const methods = ['updateAvoidPadding', 'setSplitScreen', 'cancelSplitScreen'];
  return {bodies: methods.map(name => C.prototype[name].toString()), observe(method, kind, mask, scalarKind, failAt = 0) {
    current = {trace: [], steps: 0, failAt};
    current.failure = {get code() {event('failure-code'); return 7;}, get message() {event('failure-message'); return 'original';}};
    let padding;
    const info = {set avoidAreaPadding(value) {
      assert.equal(this, info); event('set-padding');
      assert.equal(Object.getPrototypeOf(value), vm.runInContext('Object.prototype', context));
      padding = Object.entries(Object.getOwnPropertyDescriptors(value)).map(([key, descriptor]) =>
        [key, descriptor.value, descriptor.writable, descriptor.enumerable, descriptor.configurable]);
    }};
    const promise = {get catch() {event('get-catch'); return function(callback) {
      assert.equal(this, promise); event('catch-call');
      if (scalarKind === 'callback') callback(current.failure);
      return resultToken;
    };}};
    const host = new Proxy({}, {get(_, key, receiver) {
      assert.equal(receiver, host); event('get-host-method', key); return function(...args) {
        assert.equal(this, host);
        event('host-call', key, ...args.map(value => Object.entries(value)));
        return promise;
      };
    }});
    const ui = {get px2vp() {assert.equal(this, ui); event('get-px2vp'); return function(value) {
      assert.equal(this, ui); event('px2vp-call', value); return resultToken;
    };}, get getHostContext() {assert.equal(this, ui); event('get-host-context'); return function(...args) {
      assert.equal(this, ui); assert.equal(args.length, 0); event('host-context-call');
      return kind === 'host-null' ? null : kind === 'host-undefined' ? undefined : host;
    };}};
    const owner = Object.create(C.prototype);
    let reads = 0;
    Object.defineProperty(owner, 'uiContext', {get() {
      assert.equal(this, owner); event('ui-context');
      if (kind === 'mutating' && ++reads > 1) return null;
      return kind === 'null' ? null : kind === 'undefined' ? undefined : ui;
    }});
    Object.defineProperty(owner, 'mainWindowInfo', {get() {assert.equal(this, owner); event('window-info'); return info;}});
    const scalar = (value, label) => scalarKind === 'coercion' ? {
      [Symbol.toPrimitive](hint) {event('coerce', label, hint); return value;}
    } : scalarKind === 'number' || scalarKind === 'callback' ? value : scalarKind;
    const area = {};
    for (const [i, name] of ['topRect', 'leftRect', 'rightRect', 'bottomRect'].entries()) {
      const rectangle = {};
      for (const key of ['height', 'width', 'top', 'left']) Object.defineProperty(rectangle, key, {get() {
        event('rectangle-value', name, key);
        return scalar(key === 'height' || key === 'width' ? mask & (1 << i) ? 10 : 0 : 2, `${name}.${key}`);
      }});
      Object.defineProperty(area, name, {get() {event('rectangle', name); return rectangle;}});
    }
    let escaped = false, returned, typeError = false, originalFailure = false;
    try {returned = C.prototype[method].call(owner, ...(method === 'updateAvoidPadding' ? [area] : ['bundle', 'ability', 'module']));}
    catch (error) {
      escaped = true;
      if (error === current.failure) originalFailure = true;
      else if (error?.name === 'TypeError') typeError = true;
      else throw error;
    }
    return {trace: current.trace, padding, returned, escaped, typeError, originalFailure};
  }};
}
const actual = await load(report.source), expected = await load(reference);
for (const [i, count] of [4, 1, 1].entries()) {
  assert.doesNotMatch(actual.bodies[i], /INCOMPLETE|UNRECOVERED/);
  assert.equal((actual.bodies[i].match(/\?\./g) ?? []).length, count);
  assert.doesNotMatch(actual.bodies[i], /=== null|!== null/);
}
let scenarios = 0;
function compare(method, kind, mask, scalar, failAt = 0) {
  assert.deepEqual(actual.observe(method, kind, mask, scalar, failAt), expected.observe(method, kind, mask, scalar, failAt),
    `${method} kind=${kind} mask=${mask} failAt=${failAt}`);
  ++scenarios;
}
for (const kind of ['null', 'undefined', 'live', 'mutating'])
  for (let mask = 0; mask < 16; ++mask)
    for (const scalar of ['number', 'coercion', undefined, null, false, true, 0, -0, -1, NaN, Infinity, '1', 1n])
      compare('updateAvoidPadding', kind, mask, scalar);
for (const kind of ['null', 'undefined', 'live', 'mutating'])
  for (const mask of [0, 1, 5, 15]) for (const scalar of ['number', 'coercion'])
    for (let failAt = 1; failAt <= 100; ++failAt) compare('updateAvoidPadding', kind, mask, scalar, failAt);
for (const method of ['setSplitScreen', 'cancelSplitScreen'])
  for (const kind of ['null', 'undefined', 'live', 'mutating', 'host-null', 'host-undefined'])
    for (const scalar of ['number', 'callback']) for (let failAt = 0; failAt <= 20; ++failAt) compare(method, kind, 0, scalar, failAt);
assert.equal(scenarios, 4536);
for (const [from, to, kind] of [
  ['?.px2vp(', '.px2vp(', 'null'],
  ['this.uiContext', '(this.uiContext, this.uiContext)', 'mutating'],
  ['?.px2vp(', '?.px2vp.call(void 0, ', 'live'],
]) {
  assert.ok(report.source.includes(from));
  const mutated = await load(report.source.replace(from, to));
  assert.throws(() => assert.deepEqual(mutated.observe('updateAvoidPadding', kind, 1, 'number'),
    expected.observe('updateAvoidPadding', kind, 1, 'number')));
}
console.log(JSON.stringify({pairedMethodScenarios: scenarios, mutationChecks: 3,
  completeRecord: false, platformExecution: false}));
