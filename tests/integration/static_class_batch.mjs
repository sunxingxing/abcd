import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { stripTypeScriptTypes } from 'node:module';
import vm from 'node:vm';
import {GraphTrace, realmIntrinsics} from './graph_trace.mjs';
import {parseJavaScript} from './class_reference.mjs';
import {nodes} from './record_style.mjs';
const [tool, root] = process.argv.slice(2);
// Evaluation fixtures only. Production receives the ABC path, never these sources.
const cases = [
  ['Immersive', 7395, 'Immersive_src/commons/src/main/ets/utils/Breakpoint.ts'],
  ['Immersive', 16247, 'Immersive_src/shopping/src/main/ets/constants/HomeConstants.ts'],
  ['Immersive', 7167, 'Immersive_src/commons/src/main/ets/constants/CommonConstants.ts'],
  ['MultiCommunityApplication', 19020, 'src/base/src/main/ets/constants/BreakpointConstants.ts'],
  ['MultiCommunityApplication', 19442, 'src/base/src/main/ets/constants/CommonConstants.ts'],
  ['MultiCommunityApplication', 20830, 'src/detail/src/main/ets/constants/CommonConstants.ts'],
  ['MultiCommunityApplication', 31170, 'src/hot/src/main/ets/constants/CommonConstants.ts'],
  ['MultiCommunityApplication', 47487, 'src/rank/src/main/ets/constants/CommonConstants.ts'],
  ['pasteboard', 4182, 'pasteboard_src/src/main/ets/common/CommonConstants.ts'],
];
async function load(source, dependency) {
  const context = vm.createContext({ WidthBreakpoint: Object.freeze({ WIDTH_SM: 'sm', WIDTH_MD: 'md', WIDTH_LG: 'lg' }) });
  // Capture realm identities before evaluation. No constructor-name heuristic:
  // a field instance must point at the exact imported class prototype.
  const intrinsics = realmIntrinsics(context);
  const module = new vm.SourceTextModule(source, { context });
  const modules = new Map([['root', module]]);
  await module.link(specifier => {
    assert.equal(specifier, '@normalized:N&&&commons/Index&1.0.0');
    assert.ok(dependency, 'missing independent dependency fixture');
    if (!modules.has(specifier)) modules.set(specifier, new vm.SourceTextModule(dependency, { context }));
    return modules.get(specifier);
  });
  const names = Reflect.ownKeys(module.namespace).filter(key => typeof key === 'string');
  for (const name of names) assert.throws(() => module.namespace[name], { name: 'ReferenceError' });
  await module.evaluate();
  const graph = new GraphTrace().exports(modules, intrinsics);
  const before = Object.values(module.namespace);
  await module.evaluate();
  assert.deepEqual(new GraphTrace().exports(modules, intrinsics), graph, 'module re-evaluation preserves its object graph');
  for (const [index, value] of Object.values(module.namespace).entries()) assert.equal(value, before[index]);
  return {namespace: module.namespace, graph};
}
function same(a, b) {
  if (b === null || typeof b !== 'object') { assert.ok(Object.is(a, b)); return; }
  assert.equal(Array.isArray(a), Array.isArray(b));
  assert.deepEqual(Reflect.ownKeys(a), Reflect.ownKeys(b));
  for (const key of Reflect.ownKeys(b)) {
    const x = Object.getOwnPropertyDescriptor(a, key), y = Object.getOwnPropertyDescriptor(b, key);
    for (const flag of ['writable', 'enumerable', 'configurable']) assert.equal(x[flag], y[flag]);
    same(x.value, y.value);
  }
}
function separate(a, b) {
  if (a === null || typeof a !== 'object') return;
  assert.notEqual(a, b);
  for (const key of Object.keys(a)) separate(a[key], b[key]);
}
const temporary = mkdtempSync(join(tmpdir(), 'abcd-static-class-batch-'));
let graphMutations = 0;
try {
  const manifests = new Map();
  for (const app of new Set(cases.map(item => item[0]))) {
    const destination = join(temporary, app);
    const run = spawnSync(tool, [join(root, app, 'modules.abc'), destination, '--value-source'],
      { encoding: 'utf8', timeout: 60_000 });
    assert.equal(run.status, 0, run.stderr + run.stdout);
    manifests.set(app, JSON.parse(readFileSync(join(destination, 'manifest.json'), 'utf8')));
  }
  for (const [app, entity, paired] of cases) {
    const record = manifests.get(app).records.find(row => row.entity === entity);
    assert.equal(record.source_complete, true);
    assert.equal(record.legacy_writer_used, undefined);
    const source = readFileSync(join(temporary, app, record.source), 'utf8');
    assert.doesNotMatch(source, /INCOMPLETE/);
    const dependency = entity === 16247 ? stripTypeScriptTypes(readFileSync(join(root, app,
      'Immersive_src/commons/src/main/ets/utils/Breakpoint.ts'), 'utf8')) : undefined;
    const dependencyRecord = entity === 16247 ? manifests.get(app).records.find(row => row.entity === 7395) : null;
    const actualDependency = dependencyRecord ? readFileSync(join(temporary, app, dependencyRecord.source), 'utf8') : dependency;
    const actualRecord = await load(source, actualDependency), otherRecord = await load(source, actualDependency);
    const actual = actualRecord.namespace, other = otherRecord.namespace;
    const pairedSource = stripTypeScriptTypes(readFileSync(join(root, app, paired), 'utf8'));
    const expectedRecord = await load(pairedSource, dependency), expected = expectedRecord.namespace;
    assert.deepEqual(actualRecord.graph, expectedRecord.graph, app + ':' + entity + ' complete static graph');
    assert.deepEqual(otherRecord.graph, expectedRecord.graph, app + ':' + entity + ' fresh static graph');
    const arrays = nodes(parseJavaScript(source)).filter(node => node.type === 'ArrayExpression' && node.elements.length === 0)
      .sort((a, b) => a.start - b.start);
    if (arrays.length >= 2) {
      const [first, second] = arrays;
      assert.ok(!source.includes('__sharedArray'));
      const shared = 'const __sharedArray = [];\n' + source.slice(0, first.start) + '__sharedArray' +
        source.slice(first.end, second.start) + '__sharedArray' + source.slice(second.end);
      const replace = expression => source.slice(0, first.start) + expression + source.slice(first.end);
      for (const changed of [shared, replace('Object.setPrototypeOf([], null)'), replace('Object.preventExtensions([])')]) {
        parseJavaScript(changed);
        const mutant = await load(changed, actualDependency);
        // Reproduce the old blind spot: leaf values/descriptors still match.
        // Only the shared graph exposes alias, prototype or extensibility loss.
        for (const name of Object.keys(expected)) for (const key of Reflect.ownKeys(expected[name]))
          if (key !== 'prototype') same(mutant.namespace[name][key], expected[name][key]);
        assert.notDeepEqual(mutant.graph, expectedRecord.graph, 'undetected static graph mutation');
        ++graphMutations;
      }
    }
    if (entity === 16247) {
      assert.doesNotMatch(source, /\$value|\.call\(/);
      assert.match(source, /static PAGE_COL_PADDING = .*new .*BreakpointType/);
      assert.equal(actual.HomeConstants.PAGE_COL_PADDING.constructor.name, 'BreakpointType');
      for (const candidate of [source, pairedSource]) {
        await assert.rejects(load(candidate,
          'export class BreakpointType { constructor() { throw new Error("constructor sentinel"); } }'),
          { message: 'constructor sentinel' });
        // A cyclic dependency evaluates HomeConstants before the imported let
        // initializes. Native module read must preserve its ReferenceError.
        const module = new vm.SourceTextModule(candidate, { identifier: 'home' });
        const cycle = new vm.SourceTextModule('import "home"; export let BreakpointType = class {};');
        await module.link(specifier => specifier === 'home' ? module : cycle);
        await assert.rejects(cycle.evaluate(), { name: 'ReferenceError' });
      }
    }
    assert.deepEqual(Object.keys(actual), Object.keys(expected));
    for (const name of Object.keys(expected)) {
      const a = actual[name], b = expected[name];
      assert.equal(a.name, b.name); assert.equal(a.length, b.length);
      assert.deepEqual(Reflect.ownKeys(a), Reflect.ownKeys(b));
      for (const key of Reflect.ownKeys(b)) {
        const x = Object.getOwnPropertyDescriptor(a, key), y = Object.getOwnPropertyDescriptor(b, key);
        for (const flag of ['writable', 'enumerable', 'configurable']) assert.equal(x[flag], y[flag]);
        if (key !== 'prototype') { same(x.value, y.value); separate(x.value, other[name][key]); }
      }
      assert.deepEqual(Reflect.ownKeys(a.prototype), Reflect.ownKeys(b.prototype));
      assert.equal(a.prototype.constructor, a);
      assert.equal(Object.getPrototypeOf(new a()), a.prototype);
    }
    if (entity === 7395) {
      const values = [{value: 1}, {value: 2}, {value: 3}, {value: 4}];
      const a = new actual.BreakpointType(...values), b = new expected.BreakpointType(...values);
      for (const key of ['sm', 'md', 'lg', 'xl', 'unknown', undefined, {toString() { throw new Error('coercion'); }}])
        assert.equal(a.getValue(key), b.getValue(key));
      a.md = b.md = {changed: true}; assert.equal(a.getValue('md'), b.getValue('md'));
    }
    if (app === 'pasteboard') {
      const values = [{value: 1}, {value: 2}, {value: 3}];
      const a = new actual.BreakpointType(...values), b = new expected.BreakpointType(...values);
      for (const key of ['sm', 'md', 'lg', 'unknown', undefined, {toString() { throw new Error('coercion'); }}])
        assert.equal(a.GetValue(key), b.GetValue(key));
      a.sm = b.sm = {changed: true}; assert.equal(a.GetValue('sm'), b.GetValue('sm'));
    }
    console.log(`${app} record=${entity} paired class exports passed`);
  }
  assert.ok(graphMutations > 0);
  console.log(JSON.stringify({records: cases.length, graphMutations, prototypeAndAliasGraphs: true}));
} finally { rmSync(temporary, { recursive: true, force: true }); }
