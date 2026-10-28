import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {readFileSync, mkdtempSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {classReference, parseJavaScript, classLayouts} from './class_reference.mjs';
import {communityCases, communityAbilityCases} from './community_graph_state.mjs';
import {fileURLToPath} from 'node:url';
import {editExpression, memberName, callsMember} from './expression_edits.mjs';
import {classMethodProfiles} from './record_style.mjs';
import {verifySignatures} from './signature_shapes.mjs';
import {cachedVerdict} from './result_cache.mjs';

const [tool, root, option] = process.argv.slice(2);
assert.ok(option === undefined || option === '--ability');
const ability = option === '--ability';
const abc = join(root, 'modules.abc');
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '45ffdc7d5075b4de3f1c6860dcbc929bae048097af4478d09136afbec1b6c5a8');
const directory = mkdtempSync(join(tmpdir(), 'abcd-community-graph-'));
const sources = {}, reference = {};
try {
  const output = join(directory, 'output');
  execFileSync(tool, [abc, output, '--value-source', '--language-source'],
    {encoding: 'utf8', timeout: 60_000});
  const manifest = JSON.parse(readFileSync(join(output, 'manifest.json')));
  assert.equal(manifest.source_contract, 'language_source_v10');
  for (const record of manifest.records) if (record.source) {
    assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, undefined);
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
for (const name of Object.keys(sources)) {
  assert.deepEqual(classLayouts(sources[name]), classLayouts(reference[name]), 'native class representation ' + name);
  verifySignatures(sources[name], reference[name], 'native signatures ' + name);
  // Native operation inventories supplement the full runtime trace below;
  // they are not a proof of operation order or arbitrary equivalence.
  assert.deepEqual(classMethodProfiles(sources[name]), classMethodProfiles(reference[name]), 'native method bodies ' + name);
  assert.doesNotMatch(sources[name], /\b(?:goto|switchPc|UNRECOVERED|INCOMPLETE)\b/);
}

// The runtime graph below is memoized by content; see result_cache.mjs.
async function verifyCommunityGraph() {
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
// Select mutations by the expression's public operation. Private bindings and
// snapshots may be renamed, merged or eliminated by the common variable passes.
const fieldWrite = (field, right, replacement) => source => editExpression(source,
  node => node.type === 'AssignmentExpression' && node.operator === '=' && memberName(node.left) === field && right(node.right),
  (node, text) => `${text(node.left)} = ${replacement(node.right, text)}`);
const breakpointCall = (node, dependsOn) => callsMember(node, 'updateBreakpoint') &&
  node.arguments.some(argument => dependsOn(argument, value => callsMember(value, 'getWindowProperties')));
const breakpointReceiver = receiver => source => editExpression(source, breakpointCall,
  (node, text) => `${receiver}.updateBreakpoint(${node.arguments.map(text).join(', ')})`);
const mutants = [
  ['base/src/main/ets/constants/BreakpointType', 'return this.md;', 'return this.sm;', {breakpoint: 'md', width: 700}],
  ['detail/src/main/ets/viewmodel/CardViewModel', 'wrong card name', fieldWrite('name',
    node => memberName(node) === 'name', (node, text) => `${text(node.object)}.icon`)],
  ['detail/src/main/ets/viewmodel/CardViewModel', 'index % PICTURE_ARRAY_LIST.length', 'index + PICTURE_ARRAY_LIST.length'],
  ['base/src/main/ets/viewmodel/CommentViewModel', 'this.favorCount = item.favorCount', 'this.favorCount = 0'],
  ['detail/src/main/ets/view/CommentInputView', 'this.__content.set(newValue)', 'this.__content.set("")'],
  ['hot/src/main/ets/view/HotTitleView', 'wrong found selection', fieldWrite('isFoundSelected',
    node => node.type === 'Literal' && node.value === false, () => 'true')],
  ['phone/src/main/ets/pages/MainPage', 'name: "mainPage"', 'name: "unknown"'],
  ['phone/src/main/ets/pages/MainPage', 'super(parent, __localStorage, elmtId, extraInfo)', 'super(__localStorage, parent, elmtId, extraInfo)', {constructOnly: true, argumentCount: 6}],
  ['phone/src/main/ets/view/TabContentView', 'this.__currentIndex.set(newValue)', 'this.__currentIndex.set(0)'],
  ['picture/src/main/ets/view/PictureDetail', 'wrong next card index', source => editExpression(source,
    node => node.type === 'BinaryExpression' && node.operator === '+' && memberName(node.left) === 'selectCardIndex',
    (node, text) => `${text(node.left)} - ${text(node.right)}`)],
  ['detail/src/main/ets/view/CommentInputView', 'focusControl.requestFocus(TEXTAREA_KEY)', 'focusControl.requestFocus(BUTTON_KEY)'],
  ['hot/Index', 'HotPointPage as HotPointPage', 'HotPointPage as WrongPage'],
];
if (ability) mutants.push(
  ['phone/src/main/ets/entryability/EntryAbility', 'windowWidth / display.getDefaultDisplaySync().densityPixels',
    'windowWidth * display.getDefaultDisplaySync().densityPixels', {width: 700, density: 2}],
  ['phone/src/main/ets/entryability/EntryAbility', 'wrong breakpoint boundary', source => editExpression(source,
    node => node.type === 'BinaryExpression' && node.operator === '<' && node.right.type === 'MemberExpression' &&
      memberName(node.right.object) === 'BREAKPOINT_RANGES' && node.right.property.value === 1,
    (node, text) => `${text(node.left)} <= ${text(node.right)}`), {width: 600}],
  ['phone/src/main/ets/entryability/EntryAbility', 'wrong breakpoint receiver', breakpointReceiver('data')],
  ['phone/src/main/ets/entryability/EntryAbility', 'windowSize.width', '840', {resizeWidths: [599]}],
  ['phone/src/main/ets/entryability/EntryAbility', 'wrong stored breakpoint', source => editExpression(source,
    node => callsMember(node, 'setOrCreate') && node.arguments[0]?.value === 'currentBreakpoint',
    (node, text) => `${text(node.callee)}(${text(node.arguments[0])}, "lg")`)],
  ['phone/src/main/ets/entryability/EntryAbility', 'data.setWindowLayoutFullScreen(true)',
    'data.setWindowLayoutFullScreen(false)'],
  ['phone/src/main/ets/entryability/EntryAbility', 'JSON.stringify(data)', 'JSON.stringify(err)'],
  ['phone/src/main/ets/entryability/EntryAbility', 'windowStage.loadContent("pages/MainPage",',
    'return windowStage.loadContent("pages/MainPage",'],
  ['base/src/main/ets/utils/Logger', 'this.domain = 65280', 'this.domain = 0'],
  ['base/src/main/ets/utils/Logger', 'hilog.debug(this.domain, this.prefix, this.format, args)',
    'hilog.debug(this.domain, this.prefix, this.format, ...args)'],
);
// Prepare and parse every mutant before the expensive independent graph runs.
// A stale selector is a test failure, never a silently skipped negative case.
const preparedMutants = mutants.map(([name, label, edit, extra]) => {
  if (typeof edit === 'string') assert.ok(sources[name].includes(label), 'absent mutation anchor ' + name + ':' + label);
  const changed = typeof edit === 'function' ? edit(sources[name]) : sources[name].replace(label, edit);
  assert.notEqual(changed, sources[name]); parseJavaScript(changed);
  return {name, label, changed, extra};
});

// One graph pair per bounded worker: even two large md/fold scenarios exceeded
// 512 MiB in a worker; a single long-lived run exhausted the default V8 heap.
// Retain the full trace comparison and release each independent VM afterwards.
const cases = ability ? communityAbilityCases() : communityCases(), views = new Set(), rendered = new Set(), instances = new Set(), events = [];
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
  const config = {immediate, breakpoint: 'sm', ...(ability ? {ability: true, coerce: true, width: 700, density: 1, resizeWidths: [840]} : {}), storage: {isShowInput: true, isFoldHorizontal: true}};
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

let mutations = 0;
for (const {name, label, changed, extra} of preparedMutants) {
  const result = run({sources: {...sources, [name]: changed}, mutation: name + ':' + label,
    cases: [{breakpoint: 'sm', ...(ability ? {ability: true, width: 599, density: 1, resizeWidths: []} : {}), storage: {isShowInput: true, isFoldHorizontal: true}, ...extra}]});
  assert.equal(result.mutations, 1); mutations += result.mutations;
}
let equivalents = 0;
if (ability) {
  const name = 'phone/src/main/ets/entryability/EntryAbility';
  const changed = breakpointReceiver('this')(sources[name]); parseJavaScript(changed);
  for (const immediate of [false, true]) {
    assert.equal(run({sources: {...sources, [name]: changed}, cases: [{ability: true, immediate,
      concurrent: true, width: 599, density: 1, resizeWidths: [840],
      storage: {isShowInput: true, isFoldHorizontal: true}}]}).cases, 1);
    ++equivalents;
  }
}
return {cases: completed, faults, swaps, mutations, equivalents, boundaryCounts,
  events: {min: Math.min(...events), max: Math.max(...events)}, viewClasses: views.size, renderedViewClasses: rendered.size,
  generatedModules: 45, ability, platformExecution: false};
}
console.log(JSON.stringify(await cachedVerdict(
  {tool, name: 'community_graph', inputs: {sources, reference, ability}}, verifyCommunityGraph)));
