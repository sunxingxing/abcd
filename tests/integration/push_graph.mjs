import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {readFileSync, mkdtempSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {classReference, classLayouts} from './class_reference.mjs';

const [tool, root, option] = process.argv.slice(2);
const abc = join(root, 'modules.abc'), sources = {}, reference = {};
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  'b2074425e0e2a4a20b547c8b711727b0160d299efc8eeb2af266aa6983eab44f');
const directory = mkdtempSync(join(tmpdir(), 'abcd-push-graph-'));
try {
  const output = join(directory, 'output');
  execFileSync(tool, [abc, output, '--value-source', '--language-numeric', '--language-constructors'],
    {encoding: 'utf8', timeout: 60_000});
  const manifest = JSON.parse(readFileSync(join(output, 'manifest.json')));
  assert.equal(manifest.source_contract, 'language_numeric_constructors_v1');
  for (const record of manifest.records) if (record.source) {
    assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, false);
    assert.equal(record.source_route, 'value_tree');
    const name = record.source.slice('sources/'.length, -'.mjs'.length);
    sources[name] = readFileSync(join(output, record.source), 'utf8');
    assert.doesNotMatch(sources[name], /INCOMPLETE|UNRECOVERED/);
  }
} finally {rmSync(directory, {recursive: true});}
assert.equal(Object.keys(sources).length, 15);
// Sidecar input begins after ABC-only generation. Preserve every method,
// initializer and callback; erase only compiler-erased uninitialized fields.
for (const name of Object.keys(sources)) {
  reference[name] = classReference(readFileSync(join(root, 'src', name + '.ts'), 'utf8')).source;
  assert.deepEqual(classLayouts(sources[name]), classLayouts(reference[name]), 'native class representation ' + name);
}
const worker = fileURLToPath(new URL('./push_graph_worker.mjs', import.meta.url));
const run = (cases, extra = {}, changed = sources) => {
  try {
    return JSON.parse(execFileSync(process.execPath,
      ['--max-old-space-size=512', '--experimental-vm-modules', worker],
      {input: JSON.stringify({sources: changed, reference, cases, ...extra}), encoding: 'utf8',
        timeout: 60_000, maxBuffer: 2_000_000}));
  } catch (error) {throw new Error('push worker ' + JSON.stringify(cases) + ': ' + (error.stderr ?? error.message));}
};
const baseline = {};
console.log(JSON.stringify({baseline: run([baseline], {inventory: true})}));
if (option === '--baseline') process.exit(0);
const cases = [];
for (const immediate of [false, true]) for (const reverse of [false, true])
  for (const reject of ['none', 'token', 'enabled', 'enable', 'badge', 'aaid', 'delete', 'bind', 'unbind', 'media', 'raw', 'pixel', 'incoming', 'change'])
    cases.push({immediate, reverse, reject});
for (const code of [0, 1600004, 23]) for (const enabled of [false, true])
  for (const capability of [false, true]) cases.push({code, enabled, capability, reject: 'enable'});
for (const parameters of ['absent', 'null']) cases.push({parameters});
for (const windowCode of [1, 'error']) cases.push({windowCode});
for (const value of [null, false, 0, '', 17]) cases.push({value});
for (const input of [1, 2, 3, 4, 5]) cases.push({input, noPixel: true, noInfo: true});
cases.push({baseObject: true}, {finalize: 'missing'}, {language: 'en-US', image: ''});
cases.push({instances: 2, reverse: true}, {instances: 2, immediate: true});
let count = 0;
let faults = 0, swaps = 0;
const boundaries = [];
if (option !== '--mutations') {
  for (let first = 0; first < cases.length; first += 8) {count += run(cases.slice(first, first + 8)).length; console.error('push cases ' + count + '/' + cases.length);}
  assert.equal(count, cases.length);
  for (const immediate of [false, true]) {
    const [report] = run([{immediate}], {inventory: true}); boundaries.push(report.anchors.length);
    const checks = report.anchors.flatMap(failAt => Array.from({length: 5}, (_, failureIndex) => ({immediate, failAt, failureIndex})));
    for (let first = 0; first < checks.length; first += 16) {
      const results = run(checks.slice(first, first + 16)); assert.ok(results.every(row => row.failed)); faults += results.length;
    }
    for (let first = 0; first < report.anchors.length; first += 16)
      swaps += run(report.anchors.slice(first, first + 16).map(swapAt => ({immediate, swapAt}))).length;
  }
}
const changes = [
  ['entry/src/main/ets/pages/GetTokenPage', '$local0.getToken()', 'void 0', 'token button body removed'],
  ['entry/src/main/ets/component/CallComponent', '$local0.end = true', '$local0.end = false', 'shared close state lost'],
  ['entry/src/main/ets/service/VoipCallService', 'VOIP_CALL_STATE_ACTIVE', 'VOIP_CALL_STATE_DISCONNECTED', 'wrong connected call state'],
  ['entry/src/main/ets/pages/Index', '"pushToken"', '"wrong-page"', 'navigation destination changed'],
  ['entry/src/main/ets/entryformability/EntryFormAbility', 'subscriberId: $value1', 'subscriberId: "wrong-form"', 'form proxy identity changed'],
  ['entry/src/main/ets/pages/ExamplePage', '"111***222"', '"wrong-profile"', 'wrong profile passed to actual API'],
  ['entry/src/main/ets/pages/GetTokenPage', 'Successfully got push token, please save it.', 'wrong token state', 'token state callback changed'],
  ['entry/src/main/ets/entryability/RemoteNotificationExtAbility', 'overlayIcon: $value8', 'overlayIcon: null', 'remote notification pixel identity lost'],
  ['entry/src/main/ets/entryformability/EntryFormAbility', '@ohos:app.form.FormExtensionAbility',
    '@ohos:application.BackupExtensionAbility', 'wrong provider superclass identity'],
];
let mutations = 0;
for (const [name, from, to, label] of changes) {
  assert.ok(sources[name].includes(from), label);
  const changed = {...sources, [name]: sources[name].replace(from, to)};
  classLayouts(changed[name]); run([baseline], {mutation: label}, changed); ++mutations;
}
let equivalents = 0;
for (const [name, from, to] of [
  ['entry/src/main/ets/pages/GetTokenPage', '$local0.getToken()', 'this.getToken()'],
  ['entry/src/main/ets/component/CallComponent', '$local0.end = true', 'this.end = true'],
]) {
  assert.ok(sources[name].includes(from));
  const changed = {...sources, [name]: sources[name].replace(from, to)};
  classLayouts(changed[name]); run([baseline], {}, changed); ++equivalents;
}
console.log(JSON.stringify({modules: 15, views: 7, cases: count, faults, swaps, boundaries, mutations, equivalents, platformExecution: false}));
