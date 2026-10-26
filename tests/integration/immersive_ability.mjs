import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {classReference, classLayouts} from './class_reference.mjs';
import {classMethodProfiles} from './record_style.mjs';
import {verifySignatures} from './signature_shapes.mjs';
import {editExpression, callsMember, memberName} from './expression_edits.mjs';
import {abilityName} from './immersive_ability_state.mjs';

const [tool, root] = process.argv.slice(2), abc = join(root, 'modules.abc');
const index = JSON.parse(readFileSync(new URL('../corpus/abc-index.json', import.meta.url))).entries;
const identity = index.find(row => row.corpus_id === 'paired' && row.relative_path === 'Immersive/modules.abc');
const bytes = readFileSync(abc);
assert.equal(bytes.length, identity.size); assert.equal(createHash('sha256').update(bytes).digest('hex'), identity.sha256);
const records = [abilityName, 'commons/Index', 'commons/src/main/ets/utils/WindowUtil',
  'commons/src/main/ets/utils/Breakpoint', 'commons/src/main/ets/constants/CommonConstants'];
const sources = {}, reference = {}, temporary = mkdtempSync(join(tmpdir(), 'abcd-immersive-ability-'));
try {
  const output = join(temporary, 'output');
  execFileSync(tool, [abc, output, '--value-source', '--language-source'],
    {encoding: 'utf8', timeout: 60_000});
  const manifest = JSON.parse(readFileSync(join(output, 'manifest.json')));
  assert.equal(manifest.sha256, identity.sha256); assert.equal(manifest.source_contract, 'language_source_v10');
  for (const name of records) {
    const record = manifest.records.find(row => row.source === `sources/${name}.mjs`);
    assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, undefined);
    assert.equal(record.source_route, 'value_tree');
    sources[name] = readFileSync(join(output, record.source), 'utf8');
  }
} finally {rmSync(temporary, {recursive: true, force: true});}
// The whole ABC-only process has exited. Paired bodies enter only this independent
// oracle, retaining initialized fields, constructors, all methods and callbacks.
for (const name of records) {
  let text = readFileSync(join(root, 'Immersive_src', name + '.ts'), 'utf8');
  if (name.endsWith('/WindowUtil')) {
    assert.equal([...text.matchAll(/^@Observed$/gm)].length, 1);
    text = text.replace(/^@Observed$/gm, ''); // Identity decorator in the controlled host.
  }
  reference[name] = classReference(text).source;
  assert.deepEqual(classLayouts(sources[name]), classLayouts(reference[name]), name);
  verifySignatures(sources[name], reference[name], name);
  assert.deepEqual(classMethodProfiles(sources[name]), classMethodProfiles(reference[name]), name);
}
const worker = fileURLToPath(new URL('./immersive_ability_worker.mjs', import.meta.url));
const run = (cases, changed = sources, mutation = false) => JSON.parse(execFileSync(process.execPath,
  ['--no-warnings', '--experimental-vm-modules', worker], {
    input: JSON.stringify({sources: changed, reference, cases, mutation}), encoding: 'utf8', timeout: 60_000, maxBuffer: 8_000_000,
  }));
const source = sources[abilityName];
const nativeNullish = node => node.type === 'LogicalExpression' && node.operator === '??';
// Preflight all mutations through the parser before the full behavioral matrix.
// Semantic operations locate targets; temporary names and printed parentheses do not.
const mutants = [
  [editExpression(source, nativeNullish, (node, text) => `${text(node.left)} || ${text(node.right)}`), {code: 1, json: 'false'}],
  [editExpression(source, node => callsMember(node, 'setOrCreate'), (node, text) =>
    `${text(node.callee)}(${text(node.arguments[0])}, undefined)`), {}],
  [editExpression(source, node => callsMember(node, 'loadContent'), (node, text) =>
    `${text(node.callee)}("wrong/page", ${text(node.arguments[1])})`), {}],
  [editExpression(source, node => callsMember(node, 'setUIContext'), () => 'void 0'), {}],
  [editExpression(source, node => callsMember(node, 'updateWindowInfo'), () => 'void 0'), {}],
  [editExpression(source, node => node.type === 'AssignmentExpression' && memberName(node.left) === 'windowUtil',
    (node, text) => text(node.right)), {}],
  [editExpression(source, node => node.type === 'ClassBody',
    (node, text) => '{ constructor() { super(); }' + text(node).slice(1)), {}],
  [editExpression(source, node => node.type === 'VariableDeclarator' && node.init?.type === 'ThisExpression',
    (node, text) => `${text(node.id)} = globalThis`), {}],
  [editExpression(source, node => callsMember(node, 'info') && node.arguments.some(arg => arg.value === 'Ability onDestroy'),
    () => 'void 0'), {}],
  [editExpression(source, node => node.type === 'AssignmentExpression' && memberName(node.left) === 'windowUtil',
    (node, text) => `Object.defineProperty(${text(node.left.object)}, "windowUtil", {value: ${text(node.right)}, writable: true, enumerable: true, configurable: true})`),
  {windowSetter: 'store'}],
];
const counts = {cases: 0, faults: 0, swaps: 0, mutations: 0, equivalents: 0};
const cases = [];
for (const immediate of [false, true]) for (const reverse of [false, true]) for (const baseObject of [false, true])
  for (const code of [0, 1]) for (const json of ['normal', 'undefined', 'null', 'false', 'zero', 'throw'])
    cases.push({immediate, reverse, baseObject, code, json});
for (const alternate of [false, true]) for (const baseObject of [false, true])
  for (const windowSetter of ['store', 'throw']) for (let failureIndex = 0; failureIndex < 5; ++failureIndex)
    cases.push({alternate, baseObject, windowSetter, failureIndex});
// Finite test-only batches bound VM retention without dropping cases or traces.
const batchWidth = 32;
for (let first = 0; first < cases.length; first += batchWidth) {
  run(cases.slice(first, first + batchWidth)); counts.cases += Math.min(batchWidth, cases.length - first);
}
const boundaries = [];
for (const immediate of [false, true]) {
  const [baseline] = run([{immediate}]); ++counts.cases;
  const selected = [], seen = new Set();
  for (const [id, name, key] of baseline.sites) {
    const signature = JSON.stringify([name, typeof key === 'string' ? key : null]);
    if (!seen.has(signature)) {seen.add(signature); selected.push(id);}
  }
  boundaries.push(selected.length);
  const faults = selected.flatMap(failAt => Array.from({length: 5}, (_, failureIndex) => ({immediate, failAt, failureIndex})));
  for (let first = 0; first < faults.length; first += batchWidth) {
    run(faults.slice(first, first + batchWidth)); counts.faults += Math.min(batchWidth, faults.length - first);
  }
  const swaps = selected.map(swapAt => ({immediate, swapAt}));
  for (let first = 0; first < swaps.length; first += batchWidth) {
    run(swaps.slice(first, first + batchWidth)); counts.swaps += Math.min(batchWidth, swaps.length - first);
  }
}
for (const [text, config] of mutants) {run([config], {...sources, [abilityName]: text}, true); ++counts.mutations;}
const equivalent = editExpression(source, nativeNullish, (node, text) => `(${text(node.left)}) ?? (${text(node.right)})`);
run([{immediate: false}, {immediate: true}], {...sources, [abilityName]: equivalent}); counts.equivalents += 2;
console.log(JSON.stringify({...counts, boundaries, records: records.length, realWindowUtil: true,
  lifecycleMethods: 6, callbackReplays: 8, platformExecution: false}));
