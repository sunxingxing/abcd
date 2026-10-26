import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
import path from 'node:path';
const [tool, root] = process.argv.slice(2);
const push = 'push-kit-sample-code-clientdemo-arkts';
const fixtures = [
  [push, 3444, 3585, 'abilities/PushMessageAbility'],
  [push, 4224, 4358, 'abilities/TestAbility'],
  [push, 5374, 5512, 'entryability/EntryAbility'],
  [push, 5980, 6135, 'entryability/RemoteNotificationExtAbility'],
  [push, 6274, 6413, 'entryability/VoIPUIAbility'],
  [push, 6659, 6809, 'entrybackupability/EntryBackupAbility'],
  [push, 6921, 7067, 'entryformability/EntryFormAbility'],
  ['MultiCommunityApplication', 40957, 41080, 'entryability/EntryAbility'],
  ['pasteboard', 4767, 4917, 'entryability/EntryAbility'],
  ['pasteboard', 5246, 5409, 'entrybackupability/EntryBackupAbility'],
];
// Finish all ABC-only generation before opening independent paired references.
for (const row of fixtures) {
  const [app, entity, method] = row;
  const run = spawnSync(tool, [path.join(root, app, 'modules.abc'), String(method),
    '--record-source', '--value-source', '--language-source'],
    {encoding: 'utf8', timeout: 60000, maxBuffer: 8 * 1024 * 1024});
  assert.equal(run.status, 0, run.stderr);
  const report = JSON.parse(run.stdout);
  assert.equal(report.record, entity);
  assert.equal(report.source_contract, 'language_source_v10');
  assert.equal(report.source_complete, true);
  assert.equal(report.legacy_writer_used, undefined);
  assert.doesNotMatch(report.source, /INCOMPLETE|UNRECOVERED/);
  assert.match(report.source, /constructor\(\.\.\./);
  assert.match(report.source, /super\(\.\.\./);
  assert.doesNotMatch(report.source, /Reflect\.construct|Function\(/);
  row.push(report.source);
}
// Default and explicit alias must publish and emit the same contract.
let contractCases = 0;
for (const options of [[], ['--language-source']]) {
  const [app, entity, method, , source] = fixtures[0];
  const run = spawnSync(tool, [path.join(root, app, 'modules.abc'), String(method),
    '--record-source', '--value-source', ...options], {encoding: 'utf8', timeout: 60000, maxBuffer: 8 * 1024 * 1024});
  assert.equal(run.status, 0, run.stderr);
  const report = JSON.parse(run.stdout);
  assert.equal(report.record, entity);
  assert.equal(report.source_contract, 'language_source_v10');
  assert.equal(report.source_complete, true);
  assert.equal(report.source, source);
  new vm.SourceTextModule(report.source); ++contractCases;
}
const payloads = [undefined, null, false, 0, -0, NaN, 1n, Symbol('arg'), {}, []];
const thrown = Symbol('original exception');
async function observe(source, mode, args, alternate, behavior, iterator = 'normal') {
  const trace = [], token = {}, iteratorValue = {};
  let C, requested;
  function Base(...received) {
    assert.equal(new.target, requested);
    const prefix = mode === 'bound' ? 1 : 0;
    if (iterator === 'normal') {
      assert.equal(received.length, args.length + prefix);
      for (let i = 0; i < args.length; ++i) assert.ok(Object.is(received[i + prefix], args[i]));
    } else if (iterator === 'replace') assert.deepEqual(received.slice(prefix), [iteratorValue]);
    trace.push(['base', received.length, new.target === C ? 'class' : 'alternate']);
    if (behavior === 'throw') throw thrown;
    return behavior === 'object' ? token : 5;
  }
  function LocalStorage() { trace.push(['LocalStorage', arguments.length]); return {storage: true}; }
  const context = vm.createContext({JSON, LocalStorage});
  const module = new vm.SourceTextModule(source, {context});
  await module.link(specifier => {
    const names = new Set(['default']);
    for (const line of source.split('\n')) {
      if (!line.startsWith('import ') || !line.includes(JSON.stringify(specifier)) && !line.includes("'" + specifier + "'")) continue;
      const group = line.match(/\{([^}]+)\}/);
      if (group) for (const item of group[1].split(',')) names.add(item.trim().split(/\s+as\s+/)[0]);
    }
    return new vm.SyntheticModule([...names], function() {
      for (const name of names) this.setExport(name, Base);
    }, {context});
  });
  await module.evaluate(); C = module.namespace.default;
  assert.equal(typeof C, 'function'); assert.equal(C.length, 0);
  let parent = mode === 'bound' ? Base.bind(null, 'prefix') : Base;
  if (mode === 'proxy') parent = new Proxy(Base, {construct(target, list, newTarget) {
    trace.push(['trap', list.length]); return Reflect.construct(target, list, newTarget);
  }});
  if (mode === 'arrow') parent = () => {};
  Object.setPrototypeOf(C, parent);
  function Target() {}
  requested = alternate ? Target : C;
  if (iterator !== 'normal') {
    Object.defineProperty(vm.runInContext('Array.prototype', context), Symbol.iterator, {configurable: true,
      get() {
        trace.push(['iterator-get']); if (iterator === 'throw-get') throw thrown;
        return function() {
          trace.push(['iterator-call']); if (iterator === 'throw-call') throw thrown;
          let count = 0;
          return {next() {
            trace.push(['iterator-next', count]); if (iterator === 'throw-next') throw thrown;
            return count++ ? {done: true} : {value: iteratorValue, done: false};
          }};
        };
      }});
  }
  try {
    const value = Reflect.construct(C, args, requested);
    assert.equal(value === token, behavior === 'object');
    if (value !== token) assert.equal(Object.getPrototypeOf(value), requested.prototype);
    const fields = Reflect.ownKeys(value).map(key => {
      const descriptor = Object.getOwnPropertyDescriptor(value, key);
      return [key, descriptor.value, descriptor.writable, descriptor.enumerable, descriptor.configurable];
    });
    return {trace, fields, returnedObject: value === token};
  } catch (error) {
    if (error === thrown) return {trace, originalException: true};
    assert.equal(error.name, 'TypeError', String(error)); return {trace, error: error.name};
  }
}
let pairedCases = 0;
for (const [app, entity, method, relative, source] of fixtures) {
  const base = app === push ? path.join(root, app, 'src/entry/src/main/ets') :
    app === 'pasteboard' ? path.join(root, app, 'pasteboard_src/src/main/ets') : path.join(root, app, 'src/phone/src/main/ets');
  const reference = stripTypeScriptTypes(readFileSync(path.join(base, relative + '.ts'), 'utf8'), {mode: 'transform'});
  for (const payload of payloads) for (const mode of ['direct', 'bound', 'proxy', 'arrow'])
    for (const alternate of [false, true]) for (const behavior of ['primitive', 'object', 'throw']) {
      const args = [payload, {}, payload];
      assert.deepEqual(await observe(source, mode, args, alternate, behavior), await observe(reference, mode, args, alternate, behavior), String(entity)); ++pairedCases;
    }
  for (const count of [0, 1, 2, 200, 201]) {
    const args = Array.from({length: count}, (_, i) => ({i}));
    assert.deepEqual(await observe(source, 'proxy', args, true, 'object'), await observe(reference, 'proxy', args, true, 'object')); ++pairedCases;
  }
}
// This explicit native super oracle verifies the new language declaration,
// including iterator observation that old Ark and an implicit JS constructor
// do not share. It is separate from paired-source equivalence above.
const actual = fixtures[0][4];
const reference = 'import Base from "base"; export default class Derived extends Base {constructor(...args) {super(...args);}}';
let nativeCases = 0;
for (const mode of ['direct', 'bound', 'proxy', 'arrow']) for (const alternate of [false, true])
  for (const behavior of ['primitive', 'object', 'throw'])
    for (const iterator of ['normal', 'replace', 'throw-get', 'throw-call', 'throw-next']) {
      const args = [1, {}, Symbol('arg')];
      assert.deepEqual(await observe(actual, mode, args, alternate, behavior, iterator), await observe(reference, mode, args, alternate, behavior, iterator)); ++nativeCases;
    }
// Detect independent parent/forwarding/field mutations on actual generated JS.
for (const [index, from, to] of [[0, 'super(...args)', 'super()'], [0, 'super(...args)', 'super(args)'], [8, '.curBp = ""', '.curBp = "wrong"']]) {
  const source = fixtures[index][4]; assert.ok(source.includes(from));
  const altered = source.replace(from, to); let detected = false;
  try {assert.deepEqual(await observe(altered, 'proxy', [1, 2], false, 'primitive'), await observe(source, 'proxy', [1, 2], false, 'primitive'));}
  catch (error) {if (!(error instanceof assert.AssertionError)) throw error; detected = true;}
  assert.equal(detected, true, from);
}
console.log(JSON.stringify({completeRecords: 10, contractCases, pairedConstructorCases: pairedCases, nativeSuperCases: nativeCases,
  mutationChecks: 3, pairedIteratorContamination: false, inheritedFieldSetters: false,
  wholeRecordBehavior: false, platformExecution: false}));
