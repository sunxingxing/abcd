import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {dirname, join} from 'node:path';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
import {verifyCardArrayState} from './card_array_state.mjs';
import {verifyConnectedCards} from './connected_cards.mjs';
import {verifyCardItemState, verifyCardItemPeers} from './card_item_state.mjs';

const [tool, abc, paired] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '45ffdc7d5075b4de3f1c6860dcbc929bae048097af4478d09136afbec1b6c5a8');
const run = spawnSync(tool, [abc, '30756', '--record-source', '--value-source'], {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr + run.stdout);
const report = JSON.parse(run.stdout);
assert.equal(report.route, 'value_tree'); assert.equal(report.legacy_writer_used, undefined);
assert.equal(report.source_complete, true);
// Whole-record style allows the immutable captured receiver alias and explicit
// pinned instance assignment. Class, imports, native arrow/forEach construction,
// method and default initialization must remain recognizable high-level source.
assert.match(report.source, /export default new /);
assert.ok(!report.source.includes('let $default'));
// Recover the actual peer too, before opening either independent TS oracle.
const peerRun = spawnSync(tool, [abc, '30993', '--record-source', '--value-source'], {encoding: 'utf8', timeout: 60_000});
assert.equal(peerRun.status, 0, peerRun.stderr + peerRun.stdout);
const peer = JSON.parse(peerRun.stdout);
assert.equal(peer.record, 30839); assert.equal(peer.source_complete, true);
assert.equal(peer.route, 'value_tree'); assert.equal(peer.legacy_writer_used, undefined);
const commentRun = spawnSync(tool, [abc, '20678', '--record-source', '--value-source'], {encoding: 'utf8', timeout: 60_000});
assert.equal(commentRun.status, 0, commentRun.stderr + commentRun.stdout);
const comment = JSON.parse(commentRun.stdout);
assert.equal(comment.source_complete, true); assert.equal(comment.route, 'value_tree');
assert.equal(comment.legacy_writer_used, undefined);
const pairedText = readFileSync(paired, 'utf8');
assert.match(pairedText, /export class CardArray/);
assert.match(report.source, /class CardArray \{/);
assert.match(report.source, /constructor\(\)\s*\{/);
assert.match(report.source, /CARD_LIST\.forEach\(/);
assert.match(report.source, /getInstance\(\)\s*\{\s*return this;/);
assert.doesNotMatch(report.source, /INCOMPLETE|UNRECOVERED|\(\{\["CardArray"\]/);
const reference = stripTypeScriptTypes(pairedText);
async function observe(source, size, failedAt) {
  const context = vm.createContext({});
  const trace = [], items = Array.from({length: size}, (_, index) => ({index}));
  const failure = {};
  function CardItem(item, index) {
    trace.push([index, item]);
    assert.equal(new.target, CardItem);
    assert.equal(item, items[index]);
    if (index === failedAt) throw failure;
    this.item = item; this.index = index;
  }
  const list = {forEach(callback) {
    assert.equal(this, list);
    assert.equal(callback.name, ''); assert.equal(callback.length, 2);
    for (let index = 0; index < items.length; ++index)
      Reflect.apply(callback, {cardArray: 'wrong callback receiver'}, [items[index], index]);
  }};
  const root = new vm.SourceTextModule(source, {context});
  const base = new vm.SyntheticModule(['CARD_LIST'], function() { this.setExport('CARD_LIST', list); }, {context});
  const detail = new vm.SyntheticModule(['CardItem'], function() { this.setExport('CardItem', CardItem); }, {context});
  await root.link(name => {
    assert.ok(name === '@normalized:N&&&base/Index&1.0.0' ||
      name === '@normalized:N&&&detail/src/main/ets/viewmodel/CardViewModel&1.0.0');
    return name.includes('CardViewModel') ? detail : base;
  });
  assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
  assert.throws(() => root.namespace.CardArray, {name: 'ReferenceError'});
  if (failedAt >= 0 && failedAt < size) {
    await assert.rejects(root.evaluate(), error => error === failure);
    assert.equal(trace.length, failedAt + 1);
    assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
    return;
  }
  await root.evaluate();
  assert.deepEqual(Object.keys(root.namespace), ['CardArray', 'default']);
  const Class = root.namespace.CardArray, singleton = root.namespace.default;
  assert.equal(Class.name, 'CardArray'); assert.equal(Class.length, 0);
  assert.equal(singleton.constructor, Class); assert.equal(singleton.getInstance(), singleton);
  assert.equal(Class.prototype.getInstance.call(singleton), singleton);
  assert.equal(Object.getPrototypeOf(singleton), Class.prototype);
  assert.equal(Object.getOwnPropertyDescriptor(Class.prototype, 'getInstance').enumerable, false);
  const check = instance => {
    assert.deepEqual(Object.keys(instance), ['cardArray']);
    const array = instance.cardArray;
    assert.equal(Object.getPrototypeOf(array), vm.runInContext('Array.prototype', context));
    assert.equal(array.length, size);
    for (let i = 0; i < size; ++i) {
      assert.equal(array[i].item, items[i]); assert.equal(array[i].index, i);
      assert.equal(Object.getPrototypeOf(array[i]), CardItem.prototype);
    }
  };
  check(singleton);
  const second = new Class(); check(second);
  assert.notEqual(second, singleton); assert.notEqual(second.cardArray, singleton.cardArray);
  for (let i = 0; i < size; ++i) assert.notEqual(second.cardArray[i], singleton.cardArray[i]);
  assert.equal(root.namespace.default, singleton);
  assert.equal(trace.length, size * 2);
}
let scenarios = 0;
for (const size of [0, 1, 2, 200, 201, 400]) {
  for (const failedAt of [-1, 0, Math.floor(size / 2), size - 1]) {
    await observe(report.source, size, failedAt);
    await observe(reference, size, failedAt);
    ++scenarios;
  }
}
const mutation = report.source.replace('new CardItem(item, index)', 'new CardItem(item, 0)');
assert.notEqual(mutation, report.source); new vm.SourceTextModule(mutation);
await assert.rejects(observe(mutation, 2, -1));
assert.match(report.source, /\(item, index\) =>/);
const state = await verifyCardArrayState(report.source, reference);
const peerText = readFileSync(join(dirname(paired), 'CardViewModel.ts'), 'utf8');
let erased = 0;
const peerReference = stripTypeScriptTypes(peerText.replace(/^\s*(icon|name|authorType|content): ResourceStr;\s*$/gm,
  () => { ++erased; return ''; }));
assert.equal(erased, 4);
const connected = await verifyConnectedCards(report.source, peer.source, reference, peerReference);
// Certify the whole actual CardItem too, beyond its CardArray peer path. Its
// native constructor, imports, remainder/indexing and retained arrow remain
// high-level source; an immutable activation receiver alias is permitted.
assert.match(peerText, /export class CardItem/);
assert.match(peer.source, /class CardItem \{/);
assert.match(peer.source, /constructor\(card, index\)/);
assert.match(peer.source, /PICTURE_ARRAY_LIST\[index % PICTURE_ARRAY_LIST\.length\]/);
assert.match(peer.source, /COMMENT_LIST\.forEach\(\(item\) =>/);
assert.match(peer.source, /\.commentList\.push\(new CommentItem\(item\)\)/);
assert.match(peer.source, /export \{ CardItem as "CardItem" \}/);
assert.doesNotMatch(peer.source, /INCOMPLETE|UNRECOVERED|Reflect\.construct|\.call\(|\bswitch\s*\(/);
const cardItem = await verifyCardItemState(peer.source, peerReference);
const commentText = readFileSync(join(dirname(paired), '../../../../../base/src/main/ets/viewmodel/CommentViewModel.ts'), 'utf8');
let commentErased = 0;
const commentReference = stripTypeScriptTypes(commentText.replace(/^\s*(icon|name|favorCount|content|timePosition|reviewCnt|subContent)\??: (ResourceStr|number);\s*$/gm,
  () => { ++commentErased; return ''; }));
// Seven class declarations and seven interface declarations; initialized fields
// and the entire original conditional constructor body remain untouched.
assert.equal(commentErased, 14);
const actualComments = await verifyCardItemPeers(peer.source, comment.source, peerReference, commentReference);
console.log(JSON.stringify({basicScenarios: scenarios, basicMutations: 1, ...state, ...connected, ...cardItem, ...actualComments,
  wholeRecordStyle: 'accepted', cardItemRecordStyle: 'accepted',
  localDifferences: ['captured immutable receiver alias', 'pinned assignment initializer', 'equivalent class binding']}));
