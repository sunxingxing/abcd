import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';

const [tool, root] = process.argv.slice(2), abc = join(root, 'modules.abc');
const index = JSON.parse(readFileSync(new URL('../corpus/abc-index.json', import.meta.url)));
const identity = index.entries.find(row => row.corpus_id === 'paired' && row.relative_path === 'Immersive/modules.abc');
assert.ok(identity); const bytes = readFileSync(abc);
assert.equal(bytes.length, identity.size); assert.equal(createHash('sha256').update(bytes).digest('hex'), identity.sha256);
const records = [
  ['Content', 14411, 'minigame/src/main/ets/view/Content'],
  ['Breakpoint', 7395, 'commons/src/main/ets/utils/Breakpoint'],
  ['CommonConstants', 7167, 'commons/src/main/ets/constants/CommonConstants'],
];
const sources = {}, reference = {}, directory = mkdtempSync(join(tmpdir(), 'abcd-content-record-'));
try {
  // All three real records finish ABC-only generation before paired TS is read.
  const output = join(directory, 'output');
  execFileSync(tool, [abc, output, '--value-source', '--language-numeric', '--language-constructors'], {timeout: 60_000});
  const manifest = JSON.parse(readFileSync(join(output, 'manifest.json')));
  assert.equal(manifest.source_contract, 'language_numeric_constructors_v1');
  for (const [name, entity, path] of records) {
    const record = manifest.records.find(row => row.entity === entity);
    assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, undefined);
    assert.equal(record.executable, false);
    assert.equal(record.source_route, 'value_tree'); assert.equal(record.source, 'sources/' + path + '.mjs');
    sources[name] = readFileSync(join(output, record.source), 'utf8');
    assert.doesNotMatch(sources[name], /INCOMPLETE|UNRECOVERED|Reflect\.construct|\bswitch\s*\(/);
  }
} finally {rmSync(directory, {recursive: true});}
for (const [name, , path] of records) {
  let text = readFileSync(join(root, 'Immersive_src', path + '.ts'), 'utf8');
  if (name === 'Content') {
    const declaration = /^    private __mainWindowInfo: [^;\n=]+;\n/gm;
    assert.equal((text.match(declaration) ?? []).length, 1); text = text.replace(declaration, '');
  }
  if (name === 'Breakpoint') {
    const declarations = /^    (sm|md|lg|xl): T;\n/gm;
    assert.equal((text.match(declarations) ?? []).length, 4); text = text.replace(declarations, '');
  }
  reference[name] = stripTypeScriptTypes(text, {mode: 'transform'});
}
for (const pattern of [/class Content extends/, /constructor\(parent, params, __localStorage, elmtId/,
  /new SynchedPropertyNesedObjectPU\(/, /get mainWindowInfo\(\)/, /\.observeComponentCreation2\(/,
  /\.forEachUpdateFunction\(/, /GridItem\.aspectRatio\(1\)/, /Image\.create\(image\)/,
  /Array\.from\(/, /index % colors\.length/, /JSON\.stringify\(image\) \+ index/]) assert.match(sources.Content, pattern);

const worker = fileURLToPath(new URL('./content_record_worker.mjs', import.meta.url));
const run = request => JSON.parse(execFileSync(process.execPath, ['--experimental-vm-modules', worker], {
  input: JSON.stringify({sources, reference, request}), encoding: 'utf8', timeout: 60_000,
  maxBuffer: 8 * 1024 * 1024, stdio: 'pipe',
}));
const {inventory, counts} = run({phase: 'inventory', first: 0, count: 0});
const result = {cases: 0, faults: 0, swaps: 0, assignments: 0, mutations: 0};
for (const [phase, count] of Object.entries(inventory)) for (let first = 0; first < count; first += 64) {
  const row = run({phase, first, count: Math.min(64, count - first)});
  assert.deepEqual(row.counts, counts); assert.deepEqual(row.inventory, inventory);
  for (const key of Object.keys(result)) result[key] += row[key];
}
assert.equal(result.cases, inventory.lifecycle); assert.equal(result.faults + result.swaps, inventory.faults);
assert.equal(result.assignments, inventory.assignments); assert.equal(result.mutations, inventory.mutations);
console.log(JSON.stringify({...result, counts, generatedModules: records.length, wholeRecordBehavior: true,
  wholeRecordStyle: 'accepted', platformExecution: false}));
