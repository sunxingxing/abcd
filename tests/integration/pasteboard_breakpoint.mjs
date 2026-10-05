import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
const [tool, abc, paired] = process.argv.slice(2);
// Finish ABC-only generation before opening the independent reference.
const run = spawnSync(tool, [abc, '5001', '--record-source', '--value-source', '--language-arithmetic'],
  {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr);
const report = JSON.parse(run.stdout);
assert.equal(report.record, 4767); assert.equal(report.source_complete, false);
assert.equal(report.executable, false); assert.equal(report.legacy_writer_used, false);
const reference = stripTypeScriptTypes(readFileSync(paired, 'utf8'));
async function load(source) {
  let current;
  const event = (...parts) => {
    current.trace.push(parts);
    if (++current.steps === current.failAt) throw current.failure;
  };
  const scalar = (value, name) => value === 'coercion' ? {
    [Symbol.toPrimitive](hint) {event('coerce', name, hint); return name === 'width' ? 700 : 1;}
  } : value;
  const display = {get getDefaultDisplaySync() {
    event('get-display-call'); return function() {
      assert.equal(this, display); event('display-call');
      return {get densityPixels() {event('get-density'); return scalar(current.density, 'density');}};
    };
  }};
  const storage = {get setOrCreate() {
    event('get-storage-call'); return function(...args) {
      assert.equal(this, storage); event('storage-call', ...args);
    };
  }};
  const logger = {get error() {
    event('get-log-call'); return function(...args) {assert.equal(this, logger); event('log-call', ...args);};
  }};
  const context = vm.createContext({AppStorage: storage});
  const root = new vm.SourceTextModule(source, {context});
  const modules = new Map();
  await root.link(name => {
    if (!modules.has(name)) modules.set(name, new vm.SyntheticModule(['default'], function() {
      this.setExport('default', name === '@ohos:display' ? display : name.endsWith('/Logger') ? logger :
        name === '@ohos:app.ability.UIAbility' ? class {} : {});
    }, {context}));
    return modules.get(name);
  });
  await root.evaluate();
  const C = root.namespace.EntryAbility ?? root.namespace.default;
  const method = C.prototype.updateBreakpoint;
  return {method, observe(width, density, initial, failAt = 0) {
    current = {trace: [], steps: 0, failAt, density};
    current.failure = {get code() {event('error-code'); return 17;},
      get message() {event('error-message'); return 'original failure';}};
    let state = initial;
    const owner = Object.create(C.prototype);
    Object.defineProperty(owner, 'curBp', {
      get() {assert.equal(this, owner); event('get-current'); return state;},
      set(value) {assert.equal(this, owner); event('set-current', value); state = value;}
    });
    let returned, escaped = false;
    try {returned = method.call(owner, scalar(width, 'width'));}
    catch (error) {assert.equal(error, current.failure); escaped = true;}
    return {trace: current.trace, state, returned, escaped};
  }};
}
const actual = await load(report.source), expected = await load(reference);
const body = actual.method.toString();
assert.doesNotMatch(body, /INCOMPLETE|switch\s*\(/);
assert.equal((body.match(/\(\$phi\d+ = /g) ?? []).length, 1);
assert.equal((body.match(/if \(/g) ?? []).length, 1);
assert.equal((body.match(/\?/g) ?? []).length, 2);
assert.match(body, /< 600/); assert.match(body, /< 840/);
for (const size of ['sm', 'md', 'lg']) assert.ok(body.includes(`"${size}"`));
const widths = [undefined, null, false, true, 0, -0, -1, 1, 599, 600, 601, 839, 840, 841,
  NaN, Infinity, -Infinity, '600', '', 1n, Symbol('width'), 'coercion'];
const densities = [undefined, null, false, true, 0, -0, 1, 2, -1, NaN, Infinity, '2', 1n,
  Symbol('density'), 'coercion'];
const states = ['', 'sm', 'md', 'lg', undefined, null, 0, Symbol('state')];
let scenarios = 0;
for (const width of widths) for (const density of densities) for (const state of states) {
  assert.deepEqual(actual.observe(width, density, state), expected.observe(width, density, state)); ++scenarios;
}
for (const width of [599, 600, 840, 'coercion']) for (const state of ['', 'sm', 'md', 'lg'])
  for (let failAt = 1; failAt <= 16; ++failAt) {
    assert.deepEqual(actual.observe(width, 1, state, failAt), expected.observe(width, 1, state, failAt)); ++scenarios;
  }
for (const [from, to, width] of [
  [' < 600', ' <= 600', 600],
  ['"sm" :', '"lg" :', 599],
  ['display.getDefaultDisplaySync().densityPixels',
    '(display.getDefaultDisplaySync().densityPixels, display.getDefaultDisplaySync().densityPixels)', 600]
]) {
  assert.ok(report.source.includes(from));
  const mutated = await load(report.source.replace(from, to));
  assert.throws(() => assert.deepEqual(mutated.observe(width, 1, ''), expected.observe(width, 1, '')));
}
console.log(`${scenarios} paired breakpoint cases preserve thresholds, state, receiver, effects and original exceptions; three mutations detected`);
