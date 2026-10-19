import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
import {verifyBreakpointState} from './breakpoint_state.mjs';

const [tool, abc, paired] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
// Generate the whole record before reading paired source. The global enum is a
// controlled runtime input; every constructor and method body is engine output.
const report = JSON.parse(execFileSync(tool, [abc, '7531', '--record-source', '--value-source'],
  {encoding: 'utf8', timeout: 60_000}));
assert.equal(report.record, 7395); assert.equal(report.source_complete, true);
assert.equal(report.route, 'value_tree'); assert.equal(report.legacy_writer_used, undefined);
assert.equal(report.executable, false);
const fields = ['sm', 'md', 'lg', 'xl'];
const pairedText = readFileSync(paired, 'utf8');
let erased = 0;
const reference = stripTypeScriptTypes(pairedText.replace(/^    (sm|md|lg|xl): T;$/gm,
  () => {++erased; return ''; }));
assert.equal(erased, 4);
assert.match(report.source, /class BreakpointType \{/);
assert.match(report.source, /constructor\(sm, md, lg, xl\)/);
assert.match(report.source, /getValue\(currentWidthBreakpoint\)/);
assert.match(report.source, /export \{ BreakpointType as "BreakpointType" \}/);
assert.doesNotMatch(report.source, /INCOMPLETE|UNRECOVERED|let \$value|switch\s*\(|Reflect\.|\.call\(/);
for (const field of fields) assert.ok(report.source.includes(`this.${field} = ${field}`));

const values = fields.map(field => Symbol(field));
const prohibited = {[Symbol.toPrimitive]() {assert.fail('strict lookup must not coerce its input');}};
const inputValues = [undefined, null, false, true, 0, -0, NaN, Infinity, -Infinity,
  'md', 'sm', 'lg', 'changed-sm', '', 1n, Symbol('input'), prohibited];
const failures = [undefined, null, 0, Symbol('failure'), {}];
async function load(source) {
  const context = vm.createContext({});
  let active, binding;
  const root = new vm.SourceTextModule(source, {context});
  await root.link(name => {assert.fail(`unexpected import ${name}`);});
  assert.throws(() => root.namespace.BreakpointType, {name: 'ReferenceError'});
  await root.evaluate();
  assert.deepEqual(Object.getOwnPropertyNames(root.namespace), ['BreakpointType']);
  const Type = root.namespace.BreakpointType;
  // Contextify masks exceptions from externally installed global accessors as
  // ReferenceError. Install a real realm getter to observe raw language throws.
  const install = vm.runInContext(`read => {
    delete globalThis.WidthBreakpoint;
    if (read) Object.defineProperty(globalThis, "WidthBreakpoint", {
      configurable: true, get() {return read();}
    });
  }`, context);
  assert.deepEqual(Reflect.ownKeys(Type), ['length', 'name', 'prototype']);
  assert.equal(Type.name, 'BreakpointType'); assert.equal(Type.length, 4);
  assert.deepEqual(Reflect.ownKeys(Type.prototype), ['constructor', 'getValue']);
  const method = Object.getOwnPropertyDescriptor(Type.prototype, 'getValue');
  assert.equal(method.value.name, 'getValue'); assert.equal(method.value.length, 1);
  assert.deepEqual([method.writable, method.enumerable, method.configurable], [true, false, true]);
  assert.equal(Object.hasOwn(method.value, 'prototype'), false);
  assert.throws(() => Reflect.construct(method.value, []), {name: 'TypeError'});
  assert.throws(() => Reflect.apply(Type, {}, values), {name: 'TypeError'});
  // Original parameter slots keep missing values, extras and arbitrary identity.
  let arities = 0;
  for (const count of [0, 1, 2, 3, 4, 6, 200, 201]) {
    const args = Array.from({length: count}, (_, i) => i < values.length ? values[i] : prohibited);
    const instance = new Type(...args);
    assert.deepEqual(Reflect.ownKeys(instance), fields);
    for (const [index, field] of fields.entries()) assert.deepEqual(Object.getOwnPropertyDescriptor(instance, field), {
      value: args[index], writable: true, enumerable: true, configurable: true,
    });
    assert.equal(Object.getPrototypeOf(instance), Type.prototype); ++arities;
  }
  const event = (...parts) => {
    active.trace.push(parts);
    if (active.trace.length === active.swapAt) binding = active.second;
    if (active.trace.length === active.failAt) throw active.failure;
  };
  const observe = (key, mode = 'normal', receiverKind = 'proxy', failAt = 0, failure, swapAt = 0) => {
    active = {trace: [], failAt, failure, swapAt};
    const state = Object.fromEntries(fields.map((field, i) => [field, values[i]]));
    const constants = version => new Proxy({}, {get(_target, name, receiver) {
      assert.equal(receiver, version ? active.second : active.first);
      assert.ok(['WIDTH_MD', 'WIDTH_SM', 'WIDTH_LG'].includes(name), String(name));
      event('constant', version, name);
      if (mode === 'mutate') state.md = values[3];
      if (mode === 'all-key' || mode === `${name.slice(6).toLowerCase()}-key`) return key;
      if (mode === 'md-false' && name === 'WIDTH_MD') return false;
      return name === 'WIDTH_MD' ? 'md' : name === 'WIDTH_SM' ? version ? 'changed-sm' : 'sm' : 'lg';
    }});
    active.first = constants(0); active.second = constants(1);
    binding = mode === 'null-enum' ? null : mode === 'undefined-enum' ? undefined :
      mode === 'scalar-enum' ? 0 : active.first;
    install(mode === 'missing' ? undefined : () => {
      const captured = binding; event('global'); return captured;
    });
    let receiver;
    if (receiverKind === 'instance') receiver = new Type(...values);
    else if (receiverKind === 'proxy') receiver = new Proxy(state, {get(target, field, actual) {
      assert.equal(actual, receiver); assert.ok(fields.includes(field), String(field));
      event('field', field); return target[field];
    }});
    else receiver = receiverKind;
    try {
      const returned = Reflect.apply(method.value, receiver, [key]);
      return {trace: active.trace, state: Object.values(state), returned};
    }
    catch (error) {
      if (error instanceof assert.AssertionError) throw error;
      if (Object.is(error, failure)) return {trace: active.trace, state: Object.values(state), rawThrow: true};
      assert.ok(error.name === 'TypeError' || error.name === 'ReferenceError', error.name);
      return {trace: active.trace, state: Object.values(state), error: error.name};
    }
  };
  return {observe, arities};
}
const actual = await load(report.source), expected = await load(reference);
const modes = ['normal', 'md-key', 'sm-key', 'lg-key', 'all-key', 'md-false',
  'mutate', 'null-enum', 'undefined-enum', 'scalar-enum', 'missing'];
let scenarios = 0, faults = 0, swaps = 0;
for (const key of inputValues) for (const mode of modes)
  for (const receiver of ['proxy', 'instance', undefined, null, 0, false, '', Symbol('receiver')]) {
    const baseline = expected.observe(key, mode, receiver);
    assert.deepEqual(actual.observe(key, mode, receiver), baseline); ++scenarios;
    // Cover every actual event; no constant step cap can hide a later read.
    for (let point = 1; point <= baseline.trace.length; ++point) {
      for (const failure of failures) {
        const observed = actual.observe(key, mode, receiver, point, failure);
        assert.deepEqual(observed, expected.observe(key, mode, receiver, point, failure));
        assert.equal(observed.rawThrow, true, `raw fault mode=${mode} point=${point}`); ++faults;
      }
      assert.deepEqual(actual.observe(key, mode, receiver, 0, undefined, point),
        expected.observe(key, mode, receiver, 0, undefined, point)); ++swaps;
    }
  }

// Faults in the fourth assignment, return identity, property order and inherited
// setter dispatch are checked through the same constructor oracle as Community.
const constructors = await verifyBreakpointState(report.source, reference, {
  fields, globals: {WidthBreakpoint: {WIDTH_MD: 'md', WIDTH_SM: 'sm', WIDTH_LG: 'lg'}},
  dependency: null, lookups: [['md', 1], ['sm', 0], ['lg', 2], ['other', 3]],
  argumentsList: [[undefined, null, false, true], [-0, NaN, Infinity, -Infinity],
    [Symbol('sm'), {}, prohibited, Symbol('xl')]],
});
let mutations = 0;
for (const [before, after, key, mode, swapAt = 0] of [
  ['return this.md;', 'return this.lg;', 'md', 'normal'],
  ['return this.xl;', 'return this.sm;', 'other', 'normal'],
  ['currentWidthBreakpoint === WidthBreakpoint.WIDTH_MD', 'currentWidthBreakpoint == WidthBreakpoint.WIDTH_MD', 0, 'md-false'],
  ['return this.md;', 'return globalThis.md;', 'md', 'normal'],
  ['return this.md;', 'this.md;', 'md', 'normal'],
  ['currentWidthBreakpoint === WidthBreakpoint.WIDTH_MD',
    '(void WidthBreakpoint.WIDTH_SM, currentWidthBreakpoint === WidthBreakpoint.WIDTH_MD)', 'md', 'normal'],
  ['getValue(currentWidthBreakpoint) {', 'getValue(currentWidthBreakpoint) { const saved = WidthBreakpoint;', 'sm', 'normal', 2],
]) {
  let changed = report.source.replace(before, after); assert.notEqual(changed, report.source);
  if (after.includes('const saved')) changed = changed.replaceAll('WidthBreakpoint.WIDTH_', 'saved.WIDTH_');
  new vm.SourceTextModule(changed);
  const mutant = await load(changed);
  assert.notDeepEqual(mutant.observe(key, mode, 'proxy', 0, undefined, swapAt),
    expected.observe(key, mode, 'proxy', 0, undefined, swapAt)); ++mutations;
}
const changedXl = report.source.replace('this.xl = xl;', 'this.xl = lg;');
assert.notEqual(changedXl, report.source); new vm.SourceTextModule(changedXl);
await assert.rejects(load(changedXl), {name: 'AssertionError'}); ++mutations;
assert.equal(mutations, 8);
console.log(JSON.stringify({scenarios, faults, swaps, arities: actual.arities,
  ...constructors, mutations, wholeRecordBehavior: true, wholeRecordStyle: 'accepted', platformExecution: false}));
