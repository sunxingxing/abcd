import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {classReference, classLayouts, parseJavaScript} from './class_reference.mjs';
import {editExpression, editAwait, callsMember, memberName} from './expression_edits.mjs';
import {classMethodProfiles} from './record_style.mjs';
import {verifySignatures} from './signature_shapes.mjs';

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
  execFileSync(tool, [join(root, 'modules.abc'), output, '--value-source', '--language-source'],
    {encoding: 'utf8', timeout: 60_000});
  const manifest = JSON.parse(readFileSync(join(output, 'manifest.json')));
  assert.equal(manifest.source_contract, 'language_source_v10');
  for (const record of manifest.records) if (record.source || record.incomplete_source)
    assert.equal(record.source_contract, manifest.source_contract);
  for (const name of names) {
    const record = manifest.records.find(record => record.source === 'sources/' + name + '.mjs');
    assert.equal(record.source_complete, true); assert.equal(record.source_route, 'value_tree'); assert.equal(record.legacy_writer_used, undefined);
    sources[name] = readFileSync(join(output, record.source), 'utf8'); assert.doesNotMatch(sources[name], /INCOMPLETE|UNRECOVERED/);
  }
} finally {rmSync(temporary, {recursive: true});}
// Paired source enters only the independent test after ABC-only generation.
for (const name of Object.keys(sources)) {
  reference[name] = classReference(readFileSync(join(root, 'pasteboard_src/src/main/ets', name.slice('entry/src/main/ets/'.length) + '.ts'), 'utf8')).source;
  assert.deepEqual(classLayouts(sources[name]), classLayouts(reference[name]), 'native class representation ' + name);
  verifySignatures(sources[name], reference[name], 'native signatures ' + name);
  assert.deepEqual(classMethodProfiles(sources[name]), classMethodProfiles(reference[name]), 'native method bodies ' + name);
}
// Each recovered async operand may consume its pre-suspension effect subtree.
// Check syntax independently of runtime outcomes; no temporary spelling or
// statement adjacency requirement. The paired graph still checks job/throw order.
for (const name of [page, prefix+'pages/PageA', prefix+'pages/PageB']) {
  const pending=[parseJavaScript(sources[name])];let nativeOperands=0;
  while(pending.length) {
    const node=pending.pop();
    if(node.type==='AwaitExpression'&&node.argument.type==='CallExpression')++nativeOperands;
    for(const value of Object.values(node)) {
      if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}
      else if(value?.type)pending.push(value);
    }
  }
  assert.ok(nativeOperands>0, 'native pre-suspension call operand '+name);
}
// Separate, bounded representation evidence: preserve all three loop tests.
// Check native clauses and update storage independently of private spellings.
const loops = [];let prefixUpdates=0;
const pending = [parseJavaScript(sources[page])];
while (pending.length) {
  const node = pending.pop();
  if (node.type === 'WhileStatement' || node.type === 'ForStatement') loops.push(node);
  if (node.type === 'UpdateExpression' && node.prefix) ++prefixUpdates;
  if (node.type === 'AssignmentExpression' && node.left.type === 'Identifier') {
    let result=node.right;
    while (result.type === 'SequenceExpression') result=result.expressions.at(-1);
    assert.ok(!(result.type === 'UpdateExpression' && result.prefix &&
      result.argument.type === 'Identifier' && result.argument.name === node.left.name),
      'prefix result already retained in its writable binding');
  }
  for (const child of Object.values(node)) {
    if (Array.isArray(child)) pending.push(...child.filter(item => item?.type));
    else if (child?.type) pending.push(child);
  }
}
assert.equal(loops.length, 3);assert.ok(prefixUpdates>=2, 'real loop prefix updates exercised');
assert.equal(loops.filter(loop => loop.type === 'ForStatement' && loop.update).length, 2, 'native carried-state transitions');
assert.equal(loops.filter(loop => loop.type === 'ForStatement' && loop.init).length, 3, 'native carried-state initialization');
for (const loop of loops.filter(loop => loop.type === 'ForStatement' && loop.update)) {
  const updates=[],work=[loop.update];
  while(work.length){const node=work.pop();if(node.type==='UpdateExpression')updates.push(node);
    for(const value of Object.values(node)){if(Array.isArray(value))work.push(...value.filter(item=>item?.type));else if(value?.type)work.push(value);}}
  assert.equal(updates.length,1,'one native induction update');
  assert.equal(updates[0].argument.type,'Identifier');
  assert.ok(loop.init.declarations.some(declaration=>declaration.id.name===updates[0].argument.name),
    'induction update reuses its initialized loop binding');
}
assert.ok(loops.every(loop => loop.test.type === 'BinaryExpression' && loop.test.operator === '<'), 'direct native loop comparisons without private condition snapshots');
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
const controller = (source, name) => editExpression(source,
  node => callsMember(node, 'setStyledString') && memberName(node.callee.object) === 'pasteTextController',
  (node, text) => text(node.callee.object.object) + '.' + name + '.setStyledString(' + node.arguments.map(text).join(', ') + ')');
const entryAwait = (source, kind, edit) => editAwait(source,
  node => callsMember(node, 'getData') && memberName(node.arguments[0]) === kind, edit);
const mutations = [
  [page, 'new MutableStyledString("012345")', 'new MutableStyledString("wrong")', 'editor initial text changed'],
  [page, 'imageArray.buffer.slice(0)', 'imageArray.buffer', 'media buffer copy identity lost'],
  [page, source => editExpression(source, node => callsMember(node, 'appendStyledString'),
    (node, text) => 'void (' + text(node.arguments[0]) + ')', {first: true}), null, 'initial image attachment omitted'],
  [page, 'size: {width: "57px", height: "57px"}', 'size: {width: "58px", height: "57px"}', 'attachment dimensions changed'],
  [page, source => editExpression(source,
    (node, dependsOn) => callsMember(node, 'setStyledString') && dependsOn(node.arguments[0], value => callsMember(value, 'fromHtml')),
    (node, text) => text(node.callee.object.object) + '.pasteTextController.setStyledString(' + node.arguments.map(text).join(', ') + ')'),
    null, 'html applied to wrong controller'],
  [page, source => controller(source, 'pasteController'), null, 'text applied to wrong controller'],
  [page, '$local0.pastePixelMapController.deleteSpans()', 'void 0', 'old pixel spans retained'],
  [page, source => entryAwait(source, 'MIMETYPE_TEXT_HTML', () => '"ignored"'), null, 'html entry value lost'],
  [page, source => editExpression(source, node => callsMember(node, 'addEntry') && memberName(node.arguments[0]) === 'MIMETYPE_TEXT_PLAIN',
    (node, text) => text(node.callee) + '(' + text(node.arguments[0]) + ', "wrong")'), null, 'copied plain text changed'],
  [page, source => editAwait(source, node => callsMember(node, 'setData'), (node, text) => text(node.argument)), null, 'copy no longer waits for setData'],
  [page, source => entryAwait(source, 'MIMETYPE_TEXT_PLAIN', (node, text) => text(node.argument)), null, 'text entry not awaited'],
  [page, source => editExpression(source,
    (node, dependsOn) => node.type === 'BinaryExpression' && node.operator === '<' && dependsOn(node.right, value => callsMember(value, 'getRecordCount')),
    (node, text) => '(' + text(node.left) + ' < 1)'), null, 'later records omitted'],
  [page, source => editExpression(source,
    (node, dependsOn) => node.type === 'MemberExpression' && node.computed && dependsOn(node.object, value => callsMember(value, 'getValidTypes')),
    (node, text) => text(node.object) + '[0]'), null, 'type iteration index lost'],
  [page, source => editExpression(source, (node, dependsOn) => node.type === 'BinaryExpression' &&
    ['===', '!=='].includes(node.operator) && dependsOn(node.left, value => callsMember(value, 'trim')) &&
    node.right.type === 'Literal' && node.right.value === '',
    (node, text) => '(' + text(node.left) + (node.operator === '===' ? ' !== ' : ' === ') + text(node.right) + ')'),
    null, 'html empty predicate reversed'],
  [page, source => editExpression(source, node => callsMember(node, 'addImageSpan') && node.arguments.length === 2,
    (node, text) => text(node.callee) + '(({width: 57, height: 57}), ' + text(node.arguments[1]) + ')'), null, 'pixel identity replaced by equal dimensions'],
  [page, '$local0.pathStack = context.pathStack', '$local0.pathStack = $local0.pathStack', 'ready path identity ignored'],
  [prefix + 'pages/ShareDitect', '"PageBBuilder"', '"PageABuilder"', 'share route changed'],
  [prefix + 'pages/PageB', '"PageBBuilder"', '"PageABuilder"', 'page uri changed'],
];
let changedCount = 0;
for (const [name, from, to, label] of mutations) {
  if (typeof from !== 'function') assert.ok(sources[name].includes(from), label);
  const changed = {...sources, [name]: typeof from === 'function' ? from(sources[name]) : sources[name].replace(from, to)};
  classLayouts(changed[name]); run([{}], {mutation: label}, changed); ++changedCount;
}
let equivalents = 0;
for (const [from, to] of [[source => editExpression(source,
  node => callsMember(node, 'setStyledString') && memberName(node.callee.object) === 'pasteTextController',
  (node, text) => 'this.pasteTextController.setStyledString(' + node.arguments.map(text).join(', ') + ')'), null],
  ['$local0.pathStack = context.pathStack', 'this.pathStack = context.pathStack']]) {
  if (typeof from !== 'function') assert.ok(sources[page].includes(from));
  const changed = {...sources, [page]: typeof from === 'function' ? from(sources[page]) : sources[page].replace(from, to)};
  run([{}], {}, changed); ++equivalents;
}
console.log(JSON.stringify({records: names.length, views: 4, cases: count, faults, swaps, boundaries, mutations: changedCount, equivalents, platformExecution: false}));
