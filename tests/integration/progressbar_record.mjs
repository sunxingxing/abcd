import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {classReference, classLayouts} from './class_reference.mjs';
import {editExpression, editAwait, callsMember} from './expression_edits.mjs';

const [tool, root, option] = process.argv.slice(2), sources = {}, reference = {};
const identity = JSON.parse(readFileSync(new URL('../corpus/abc-index.json', import.meta.url))).entries
  .find(row => row.corpus_id === 'paired' && row.relative_path === 'pasteboard/modules.abc');
const bytes = readFileSync(join(root, 'modules.abc'));
assert.equal(bytes.length, identity.size); assert.equal(createHash('sha256').update(bytes).digest('hex'), identity.sha256);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-progressbar-record-'));
const page = 'entry/src/main/ets/pages/ProgressBar', constants = 'entry/src/main/ets/common/CommonConstants';
try {
  const output = join(temporary, 'output');
  execFileSync(tool, [join(root, 'modules.abc'), output, '--value-source', '--language-numeric', '--language-constructors'],
    {encoding: 'utf8', timeout: 60_000});
  const manifest = JSON.parse(readFileSync(join(output, 'manifest.json')));
  assert.equal(manifest.source_contract, 'language_numeric_constructors_v1');
  for (const name of [page, constants]) {
    const record = manifest.records.find(record => record.source === 'sources/' + name + '.mjs');
    assert.equal(record.source_complete, true); assert.equal(record.source_route, 'value_tree'); assert.equal(record.legacy_writer_used, false);
    sources[name] = readFileSync(join(output, record.source), 'utf8'); assert.doesNotMatch(sources[name], /INCOMPLETE|UNRECOVERED/);
  }
} finally {rmSync(temporary, {recursive: true});}
// Paired source enters only the independent test after ABC-only generation.
for (const name of Object.keys(sources)) {
  reference[name] = classReference(readFileSync(join(root, 'pasteboard_src/src/main/ets', name.slice('entry/src/main/ets/'.length) + '.ts'), 'utf8')).source;
  assert.deepEqual(classLayouts(sources[name]), classLayouts(reference[name]), 'native class representation ' + name);
}
const worker = fileURLToPath(new URL('./progressbar_record_worker.mjs', import.meta.url));
const run = (cases, extra = {}, changed = sources) => {
  try {
    return JSON.parse(execFileSync(process.execPath, ['--max-old-space-size=512', '--experimental-vm-modules', worker],
      {input: JSON.stringify({cases, sources: changed, reference, ...extra}), encoding: 'utf8', timeout: 60_000, maxBuffer: 2_000_000}));
  } catch (error) {throw new Error('progressbar worker ' + JSON.stringify(cases) + ': ' + (error.stderr ?? error.message));}
};
console.log(JSON.stringify({baseline: run([{}], {inventory: true})}));
if (option === '--baseline') process.exit(0);
const cases = [];
for (const immediate of [false, true]) for (const reverse of [false, true])
  for (const reject of ['none', 'set', 'get', 'unlink']) for (const progressAfter of [false, true])
    cases.push({immediate, reverse, reject, progressAfter});
for (const result of [1, null, false, '', '0']) cases.push({result});
for (const breakpoint of ['sm', 'md', 'lg', '', null]) cases.push({breakpoint});
for (const progress of [[100], [100, 100], [0, 33, 99], [null, 0, 101, 100]]) cases.push({progress, renderAgain: true});
cases.push({registration: false}, {parent: 'null'}, {baseObject: true}, {finalize: 'missing'}, {noContext: true});
for (const provided of [{progressInfo: 100}, {progressInfo: null}, {gradientColor: null, controller: null, options: null, pathStack: null}])
  cases.push({provided, renderAgain: true});
for (const construct of ['default', 'given']) for (const parameterAccess of [false, true])
  for (const id of [undefined, null, 0, 17]) for (const lambda of [false, true])
    cases.push({construct, parameterAccess, id, lambda});
cases.push({construct: 'default', superProxy: true}, {construct: 'given', superProxy: true, newTarget: true, baseObject: true});
let count = 0, faults = 0, swaps = 0; const boundaries = [];
if (option !== '--mutations') {
  for (let first = 0; first < cases.length; first += 8) count += run(cases.slice(first, first + 8)).length;
  assert.equal(count, cases.length);
  for (const immediate of [false, true]) {
    const [baseline] = run([{immediate}], {inventory: true}); boundaries.push(baseline.anchors.length);
    const checks = baseline.anchors.flatMap(failAt => Array.from({length: 5}, (_, failureIndex) => ({immediate, failAt, failureIndex})));
    for (let first = 0; first < checks.length; first += 8) {
      const rows = run(checks.slice(first, first + 8)); assert.ok(rows.every(row => row.failed)); faults += rows.length;
    }
    for (let first = 0; first < baseline.anchors.length; first += 8)
      swaps += run(baseline.anchors.slice(first, first + 8).map(swapAt => ({immediate, swapAt}))).length;
  }
}
const mutations = [
  ['VALUE_TEST_STRING_ELEMENT = "a".repeat', 'VALUE_TEST_STRING_ELEMENT = "b".repeat', 'file contents changed'],
  ['$local0.progressInfo = progress.progress', '$local0.progressInfo = 99', 'progress state lost'],
  ['}), 300)', '}), 301)', 'timer delay changed'],
  ['fileIo.unlink(dstUri)', 'void dstUri', 'destination not removed'],
  [source => editExpression(source, node => callsMember(node, 'closeSync'), () => '(void 0)', {first: true}), null, 'file handle leaked'],
  ['($local0.pathStack = context.pathStack)', '($local0.pathStack = null)', 'navigation ready state lost'],
  [source => editAwait(source, node => callsMember(node, 'setData'), (node, text) => text(node.argument)), null, 'copy completion not awaited'],
  ['value: $local0.progressInfo', 'value: 99', 'progress UI value changed'],
  ['"getDataWithProgress success"', '"wrong completion"', 'completion log changed'],
  ['($local0.pathStack = context.pathStack)', '($local0.pathStack = $local0.pathStack)', 'new ready path identity discarded'],
  ['progressIndicator: pasteboard.ProgressIndicator.NONE', 'progressIndicator: 99', 'transfer indicator changed'],
  ['(this.options = ({controller: this.controller}))', '(this.options = ({controller: new RichEditorController()}))', 'controller alias split'],
  ['elmtId = (-1)', 'elmtId = 0', 'constructor default id changed', {construct: 'default', lambda: false}],
  ['paramsLambda = (void 0)', 'paramsLambda = (() => {})', 'default parameters callback fabricated', {construct: 'default', lambda: false}],
  ['super(parent, __localStorage, elmtId, extraInfo)', 'super(parent, elmtId, __localStorage, extraInfo)', 'super argument order changed'],
];
let changedCount = 0;
for (const [from, to, label, config = {}] of mutations) {
  if (typeof from !== 'function') assert.ok(sources[page].includes(from), label);
  const changed = {...sources, [page]: typeof from === 'function' ? from(sources[page]) : sources[page].replace(from, to)};
  classLayouts(changed[page]); run([config], {mutation: label}, changed); ++changedCount;
}
let equivalents = 0;
for (const [from, to] of [['$local0.progressInfo = progress.progress', 'this.progressInfo = progress.progress'],
  ['$local0.pathStack = context.pathStack', 'this.pathStack = context.pathStack']]) {
  assert.ok(sources[page].includes(from)); const changed = {...sources, [page]: sources[page].replace(from, to)};
  run([{}], {}, changed); ++equivalents;
}
console.log(JSON.stringify({records: 2, cases: count, faults, swaps, boundaries, mutations: changedCount, equivalents, platformExecution: false}));
