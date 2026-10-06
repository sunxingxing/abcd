import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {readFileSync, mkdtempSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {classReference, parseJavaScript} from './class_reference.mjs';
import {communityCases} from './community_graph_state.mjs';
import {fileURLToPath} from 'node:url';

const [tool, root] = process.argv.slice(2);
const abc = join(root, 'modules.abc');
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '45ffdc7d5075b4de3f1c6860dcbc929bae048097af4478d09136afbec1b6c5a8');
const directory = mkdtempSync(join(tmpdir(), 'abcd-community-graph-'));
const sources = {}, reference = {};
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
assert.equal(Object.keys(sources).length, 45);
// Reference preparation starts only after the ABC-only exporter exits. Preserve
// every initializer/method/closure; Node declarations without an initializer
// are erased to match the pinned compiler's absence of a property write.
for (const name of Object.keys(sources)) {
  const text = readFileSync(join(root, 'src', name + '.ts'), 'utf8');
  reference[name] = classReference(text).source;
}
// This directed representation check compares native class method order,
// getter/setter kinds and parameter/default/rest forms. Field initializers may
// live in native class fields or equivalent constructor assignments. Runtime
// graph snapshots independently compare descriptors, aliases and signatures.
const layouts = source => {
  const work = [parseJavaScript(source)], classes = [];
  while (work.length) {
    const node = work.pop();
    if (node.type === 'ClassDeclaration' || node.type === 'ClassExpression') classes.push(node);
    for (const value of Object.values(node)) {
      if (Array.isArray(value)) for (const child of value)
        if (child && typeof child.type === 'string') work.push(child);
      if (value && typeof value.type === 'string') work.push(value);
    }
  }
  return classes.sort((a, b) => a.start - b.start).map(node => [node.superClass !== null,
    node.body.body.filter(method => method.type === 'MethodDefinition' && method.kind !== 'constructor')
      .map(method => [method.kind, method.static, method.computed, method.key.name ?? method.key.value,
        method.value.async, method.value.generator, method.value.params.map(param => param.type)])]);
};
for (const name of Object.keys(sources)) {
  assert.deepEqual(layouts(sources[name]), layouts(reference[name]), 'native class representation ' + name);
  assert.doesNotMatch(sources[name], /\b(?:goto|switchPc|UNRECOVERED|INCOMPLETE)\b/);
}

const worker = fileURLToPath(new URL('./community_graph_worker.mjs', import.meta.url));
const run = request => {
  try {
    return JSON.parse(execFileSync(process.execPath,
      ['--max-old-space-size=512', '--expose-gc', '--no-warnings', '--experimental-vm-modules', worker],
      {input: JSON.stringify({sources, reference, ...request}), encoding: 'utf8', timeout: 60_000,
        maxBuffer: 2_000_000}));
  } catch (error) {
    throw new Error('community worker ' + JSON.stringify({cases: request.cases, mutation: request.mutation}) +
      ' failed: ' + (error.stderr ?? error.message));
  }
};
// One graph pair per bounded worker: even two large md/fold scenarios exceeded
// 512 MiB in a worker; a single long-lived run exhausted the default V8 heap.
// Retain the full trace comparison and release each independent VM afterwards.
const cases = communityCases(), views = new Set(), rendered = new Set(), instances = new Set(), events = [];
let completed = 0;
for (const config of cases) {
  const result = run({cases: [config]});
  assert.equal(result.cases, 1);
  for (const key of result.views) views.add(key);
  for (const key of result.instances) instances.add(key);
  for (const key of result.rendered) rendered.add(key);
  events.push(...result.counts); completed += result.cases;
}
for (const key of views) {
  assert.ok(instances.has(key), 'unconstructed actual view ' + key);
  assert.ok(rendered.has(key), 'unrendered actual view ' + key);
}
assert.equal(completed, cases.length);

let faults = 0, swaps = 0;
const boundaryCounts = [];
for (const immediate of [false, true]) {
  const config = {immediate, breakpoint: 'sm', storage: {isShowInput: true, isFoldHorizontal: true}};
  const baseline = run({cases: [config], inventory: true});
  assert.equal(baseline.anchors.length, 1);
  const anchors = baseline.anchors[0]; boundaryCounts.push(anchors.length);
  assert.equal(new Set(anchors).size, anchors.length);
  for (const ordinal of anchors) {
    for (let failureIndex = 0; failureIndex < 5; ++failureIndex) {
      assert.equal(run({cases: [{...config, failAt: ordinal, failureIndex}]}).cases, 1); ++faults;
    }
    assert.equal(run({cases: [{...config, swapAt: ordinal}]}).cases, 1); ++swaps;
  }
}

const mutants = [
  ['base/src/main/ets/constants/BreakpointType', 'return this.md;', 'return this.sm;', {breakpoint: 'md'}],
  ['detail/src/main/ets/viewmodel/CardViewModel', '$local2.name = card.name', '$local2.name = card.icon'],
  ['detail/src/main/ets/viewmodel/CardViewModel', 'index % PICTURE_ARRAY_LIST.length', 'index + PICTURE_ARRAY_LIST.length'],
  ['base/src/main/ets/viewmodel/CommentViewModel', 'this.favorCount = item.favorCount', 'this.favorCount = 0'],
  ['detail/src/main/ets/view/CommentInputView', 'this.__content.set(newValue)', 'this.__content.set("")'],
  ['hot/src/main/ets/view/HotTitleView', '$local0.isFoundSelected = false', '$local0.isFoundSelected = true'],
  ['phone/src/main/ets/pages/MainPage', 'name: "mainPage"', 'name: "unknown"'],
  ['phone/src/main/ets/pages/MainPage', 'super(parent, __localStorage, elmtId, extraInfo)', 'super(__localStorage, parent, elmtId, extraInfo)', {constructOnly: true, argumentCount: 6}],
  ['phone/src/main/ets/view/TabContentView', 'this.__currentIndex.set(newValue)', 'this.__currentIndex.set(0)'],
  ['picture/src/main/ets/view/PictureDetail', '$local0.selectCardIndex + 1', '$local0.selectCardIndex - 1'],
  ['detail/src/main/ets/view/CommentInputView', 'focusControl.requestFocus(TEXTAREA_KEY)', 'focusControl.requestFocus(BUTTON_KEY)'],
  ['hot/Index', 'HotPointPage as HotPointPage', 'HotPointPage as WrongPage'],
];
let mutations = 0;
for (const [name, before, after, extra] of mutants) {
  assert.ok(sources[name].includes(before), 'absent mutation anchor ' + name + ':' + before);
  const changed = sources[name].replace(before, after);
  assert.notEqual(changed, sources[name]); parseJavaScript(changed);
  const result = run({sources: {...sources, [name]: changed}, mutation: name + ':' + before,
    cases: [{breakpoint: 'sm', storage: {isShowInput: true, isFoldHorizontal: true}, ...extra}]});
  assert.equal(result.mutations, 1); mutations += result.mutations;
}
console.log(JSON.stringify({cases: completed, faults, swaps, mutations, boundaryCounts,
  events: {min: Math.min(...events), max: Math.max(...events)}, viewClasses: views.size, renderedViewClasses: rendered.size,
  generatedModules: 45, platformExecution: false}));
