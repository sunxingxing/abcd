import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {classReference, classLayouts, parseJavaScript} from './class_reference.mjs';

const [tool, root, option] = process.argv.slice(2), sources = {}, reference = {};
const identity = JSON.parse(readFileSync(new URL('../corpus/abc-index.json', import.meta.url))).entries
  .find(row => row.corpus_id === 'paired' && row.relative_path === 'pasteboard/modules.abc');
const bytes = readFileSync(join(root, 'modules.abc'));
assert.equal(bytes.length, identity.size); assert.equal(createHash('sha256').update(bytes).digest('hex'), identity.sha256);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-rich-text-record-'));
const prefix = 'entry/src/main/ets/', page = prefix + 'pages/PasteBoard', constants = prefix + 'common/CommonConstants';
const names = [page, constants, ...['ShareDitect', 'PageA', 'PageB'].map(name => prefix + 'pages/' + name)];
try {
  const output = join(temporary, 'output');
  execFileSync(tool, [join(root, 'modules.abc'), output, '--value-source', '--language-numeric', '--language-constructors'],
    {encoding: 'utf8', timeout: 60_000});
  const manifest = JSON.parse(readFileSync(join(output, 'manifest.json')));
  assert.equal(manifest.source_contract, 'language_numeric_constructors_v1');
  for (const name of names) {
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
// Separate, bounded representation evidence: preserve all three loop tests.
// This does not certify native for initialization/update or remove SSA carriers.
const loops = [];
const pending = [parseJavaScript(sources[page])];
while (pending.length) {
  const node = pending.pop();
  if (node.type === 'WhileStatement') loops.push(node);
  for (const child of Object.values(node)) {
    if (Array.isArray(child)) pending.push(...child.filter(item => item?.type));
    else if (child?.type) pending.push(child);
  }
}
assert.equal(loops.length, 3);
assert.ok(loops.every(loop => loop.test.type !== 'Literal' || loop.test.value !== true), 'native loop conditions');
const worker = fileURLToPath(new URL('./rich_text_record_worker.mjs', import.meta.url));
const run = (cases, extra = {}, changed = sources) => {
  try {
    return JSON.parse(execFileSync(process.execPath, ['--max-old-space-size=512', '--experimental-vm-modules', worker],
      {input: JSON.stringify({cases, sources: changed, reference, ...extra}), encoding: 'utf8', timeout: 60_000, maxBuffer: 2_000_000}));
  } catch (error) {throw new Error('rich text worker ' + JSON.stringify(cases) + ': ' + (error.stderr ?? error.message));}
};
console.log(JSON.stringify({baseline: run([{}], {inventory: true})}));
if (option === '--baseline') process.exit(0);
const cases = [];
for (const immediate of [false, true]) for (const reverse of [false, true])
  for (const reject of ['none', 'media', 'pixel', 'get', 'data', 'html', 'set']) cases.push({immediate, reverse, reject});
for (const records of [[], [[]], [['other']], [['html']], [['text']], [['pixel']], [['pixel', 'html', 'text']],
  [['html', 'pixel'], ['text'], ['html', 'text']], Array.from({length: 201}, () => ['text'])]) cases.push({records});
for (const spans of [null, [], ['empty'], ['null', 'empty', 0, false, 'pixel'], ['pixel', 'pixel']]) cases.push({spans});
for (const directHtml of ['', '  ', '<i>direct</i>', null, 0]) cases.push({directHtml});
for (const html of ['', '  ', '<b>another</b>', null, 0]) cases.push({html});
for (const breakpoint of ['sm', 'md', 'lg', '', null]) cases.push({breakpoint});
for (const result of [1, null, false, '', '0']) cases.push({result});
cases.push({registration: false}, {parent: 'null'}, {baseObject: true}, {finalize: 'missing'}, {extraType: true},
  {provided: {pasteController: null, pathStack: null}, renderAgain: true});
cases.push({shrink: true}, {records: Array.from({length: 200}, () => ['text'])});
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
  [page, 'new MutableStyledString("012345")', 'new MutableStyledString("wrong")', 'editor initial text changed'],
  [page, 'imageArray.buffer.slice(0)', 'imageArray.buffer', 'media buffer copy identity lost'],
  [page, 'mutableStyledString1.appendStyledString($value1)', 'void $value1', 'initial image attachment omitted'],
  [page, 'size: {width: "57px", height: "57px"}', 'size: {width: "58px", height: "57px"}', 'attachment dimensions changed'],
  [page, 'this.pasteController.setStyledString($value4)', 'this.pasteTextController.setStyledString($value4)', 'html applied to wrong controller'],
  [page, '$local0.pasteTextController.setStyledString($value38)', '$local0.pasteController.setStyledString($value38)', 'text applied to wrong controller'],
  [page, '$local0.pastePixelMapController.deleteSpans()', 'void 0', 'old pixel spans retained'],
  [page, '($value31 = (await $value30))', '($value31 = "ignored")', 'html entry value lost'],
  [page, '$value4.addEntry(pasteboard.MIMETYPE_TEXT_PLAIN, $value5.getString())', '$value4.addEntry(pasteboard.MIMETYPE_TEXT_PLAIN, "wrong")', 'copied plain text changed'],
  [page, '(await $value12)', '$value12', 'copy no longer waits for setData'],
  [page, '($value37 = (await $value36))', '($value37 = $value36)', 'text entry not awaited'],
  [page, '($value15 = $value14 < $value10.getRecordCount())', '($value15 = $value14 < 1)', 'later records omitted'],
  [page, '($value27 = $value20[$value26])', '($value27 = $value20[0])', 'type iteration index lost'],
  [page, 'if (!$value2) {', 'if ($value2) {', 'html empty predicate reversed'],
  [page, 'addImageSpan($value42,', 'addImageSpan(({width: 57, height: 57}),', 'pixel identity replaced by equal dimensions'],
  [page, '($local0.pathStack = context.pathStack)', '($local0.pathStack = $local0.pathStack)', 'ready path identity ignored'],
  [prefix + 'pages/ShareDitect', '"PageBBuilder"', '"PageABuilder"', 'share route changed'],
  [prefix + 'pages/PageB', '"PageBBuilder"', '"PageABuilder"', 'page uri changed'],
];
let changedCount = 0;
for (const [name, from, to, label] of mutations) {
  assert.ok(sources[name].includes(from), label);
  const changed = {...sources, [name]: sources[name].replace(from, to)};
  classLayouts(changed[name]); run([{}], {mutation: label}, changed); ++changedCount;
}
let equivalents = 0;
for (const [from, to] of [['$local0.pasteTextController.setStyledString($value38)', 'this.pasteTextController.setStyledString($value38)'],
  ['$local0.pathStack = context.pathStack', 'this.pathStack = context.pathStack']]) {
  assert.ok(sources[page].includes(from)); const changed = {...sources, [page]: sources[page].replace(from, to)};
  run([{}], {}, changed); ++equivalents;
}
console.log(JSON.stringify({records: names.length, views: 4, cases: count, faults, swaps, boundaries, mutations: changedCount, equivalents, platformExecution: false}));
