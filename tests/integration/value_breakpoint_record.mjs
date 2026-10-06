import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
import {verifyBreakpointState} from './breakpoint_state.mjs';

const [tool, abc, paired] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '45ffdc7d5075b4de3f1c6860dcbc929bae048097af4478d09136afbec1b6c5a8');
const run = spawnSync(tool, [abc, '19387', '--record-source', '--value-source'], {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr + run.stdout);
const report = JSON.parse(run.stdout);
assert.equal(report.record, 19234); assert.equal(report.route, 'value_tree');
assert.equal(report.source_complete, true); assert.equal(report.legacy_writer_used, false);
// This ABC uses assignment-field semantics. Node's type stripper retains native
// fields, so erase only these three uninitialized TS declarations in the oracle.
// This is a test reference adaptation, never an engine input or profile claim.
const pairedText = readFileSync(paired, 'utf8');
let erased = 0;
const reference = stripTypeScriptTypes(pairedText.replace(/^\s*(sm|md|lg): T;\s*$/gm, () => { ++erased; return ''; }));
assert.equal(erased, 3);
// Whole-record representation permits an equivalent mutable class binding and
// two ordered if statements with reversed final predicate polarity. Type-only
// fields remain erased; pinned writes stay native instance assignments.
assert.match(report.source, /constructor\(sm, md, lg\)/);
assert.match(report.source, /getValue\(currentBreakpoint\)/);
assert.doesNotMatch(report.source, /INCOMPLETE|UNRECOVERED|let \$value|\(\{\["BreakpointType"\]/);
assert.match(pairedText, /export class BreakpointType/);
assert.match(report.source, /let BreakpointType = class \{/);
assert.match(report.source, /^import \{ BreakpointConstants \}/);
for (const field of ['sm', 'md', 'lg']) assert.match(report.source, new RegExp(`this\\.${field} = ${field}`));
assert.match(report.source, /export \{ BreakpointType as "BreakpointType" \}/);
const specifier = '@normalized:N&&&base/src/main/ets/constants/BreakpointConstants&1.0.0';
const values = [Symbol('small'), {size: 'medium'}, Symbol('large')];
const thrown = Symbol('arbitrary thrown value');
const noncoercible = {[Symbol.toPrimitive]() { throw thrown; }};
const keys = [undefined, null, false, true, 0, -0, NaN, 1, 'md', 'lg', 'sm', '',
  Symbol('key'), noncoercible, values[1]];

async function load(source, initial) {
  const context = vm.createContext({});
  const root = new vm.SourceTextModule(source, {context});
  const dependency = new vm.SyntheticModule(['BreakpointConstants'], function() {
    this.setExport('BreakpointConstants', initial);
  }, {context});
  await root.link(name => { assert.equal(name, specifier); return dependency; });
  assert.throws(() => root.namespace.BreakpointType, {name: 'ReferenceError'});
  await root.evaluate();
  assert.deepEqual(Object.keys(root.namespace), ['BreakpointType']);
  const Class = root.namespace.BreakpointType;
  assert.equal(Class.name, 'BreakpointType'); assert.equal(Class.length, 3);
  assert.equal(Class.prototype.getValue.name, 'getValue'); assert.equal(Class.prototype.getValue.length, 1);
  const descriptor = Object.getOwnPropertyDescriptor(Class.prototype, 'getValue');
  assert.equal(descriptor.writable, true); assert.equal(descriptor.enumerable, false); assert.equal(descriptor.configurable, true);
  const instance = new Class(...values);
  assert.deepEqual(Object.keys(instance), ['sm', 'md', 'lg']);
  for (let i = 0; i < 3; ++i) assert.deepEqual(Object.getOwnPropertyDescriptor(instance, ['sm', 'md', 'lg'][i]), {
    value: values[i], writable: true, enumerable: true, configurable: true,
  });
  return {Class, instance, dependency};
}
async function observe(source, key, mode, failedAt) {
  const trace = [], constants = {};
  const loaded = await load(source, constants);
  const fields = {sm: values[0], md: values[1], lg: values[2]};
  const lookup = name => {
    trace.push(name);
    if (name === failedAt) throw thrown;
  };
  const selected = mode === 'md-key' ? key : 'md';
  Object.defineProperty(constants, 'BREAKPOINT_MD', {get() {
    lookup('constant-md');
    if (mode === 'mutate') fields.lg = values[0];
    return selected;
  }});
  Object.defineProperty(constants, 'BREAKPOINT_LG', {get() {
    lookup('constant-lg');
    return mode === 'lg-key' ? key : 'lg';
  }});
  const receiver = new Proxy(fields, {get(target, name, actual) {
    assert.equal(actual, receiver); lookup(`field-${name}`); return Reflect.get(target, name, actual);
  }});
  let result, error;
  try { result = Reflect.apply(loaded.Class.prototype.getValue, receiver, [key]); }
  catch (failure) { error = failure; assert.equal(failure, thrown); }
  return {trace, result, error, fields: Object.values(fields)};
}
let scenarios = 0;
for (const key of keys) for (const mode of ['normal', 'md-key', 'lg-key', 'mutate']) {
  for (const failedAt of ['', 'constant-md', 'constant-lg', 'field-sm', 'field-md', 'field-lg']) {
    assert.deepEqual(await observe(report.source, key, mode, failedAt), await observe(reference, key, mode, failedAt));
    ++scenarios;
  }
}
// Native ESM binding replacement must be observed after the record is evaluated.
for (const source of [report.source, reference]) {
  const loaded = await load(source, {BREAKPOINT_MD: 'md', BREAKPOINT_LG: 'lg'});
  assert.equal(loaded.instance.getValue('md'), values[1]);
  loaded.dependency.setExport('BreakpointConstants', {BREAKPOINT_MD: 'changed', BREAKPOINT_LG: 'md'});
  assert.equal(loaded.instance.getValue('md'), values[2]);
  assert.equal(loaded.instance.getValue('changed'), values[1]);
  assert.equal(loaded.instance.getValue('other'), values[0]);
  for (const failedAt of ['', 'md', 'lg']) {
    const trace = [];
    for (const field of ['sm', 'md', 'lg']) Object.defineProperty(loaded.Class.prototype, field, {configurable: true,
      set(value) { trace.push([field, value, this]); if (field === failedAt) throw thrown; },
    });
    let instance;
    if (failedAt) assert.throws(() => new loaded.Class(...values), error => error === thrown);
    else instance = new loaded.Class(...values);
    const count = failedAt === 'md' ? 2 : 3;
    assert.deepEqual(trace.map(([field, value]) => [field, value]), ['sm', 'md', 'lg'].slice(0, count).map((field, i) => [field, values[i]]));
    assert.ok(trace.every(([, , receiver]) => receiver === trace[0][2]));
    if (instance) { assert.equal(trace[0][2], instance); assert.deepEqual(Object.keys(instance), []); }
  }
}
const mutations = [
  report.source.replace('return this.md;', 'return this.lg;'),
  report.source.replace('currentBreakpoint === BreakpointConstants.BREAKPOINT_MD',
    'currentBreakpoint === BreakpointConstants.BREAKPOINT_LG'),
  report.source.replace('currentBreakpoint === BreakpointConstants.BREAKPOINT_MD',
    '(void BreakpointConstants.BREAKPOINT_LG, currentBreakpoint === BreakpointConstants.BREAKPOINT_MD)'),
];
for (const mutation of mutations) {
  assert.notEqual(mutation, report.source); new vm.SourceTextModule(mutation);
  const actual = await observe(mutation, 'md', 'normal', '');
  const expected = await observe(reference, 'md', 'normal', '');
  assert.throws(() => assert.deepEqual(actual, expected));
}
const state = await verifyBreakpointState(report.source, reference);
console.log(JSON.stringify({lookupScenarios: scenarios, lookupMutations: 3, ...state, wholeRecordStyle: 'accepted',
  localDifferences: ['equivalent class binding', 'equivalent final if polarity', 'erased type-only fields']}));
