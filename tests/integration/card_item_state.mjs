import assert from 'node:assert/strict';
import vm from 'node:vm';

const Mode = Object.freeze(Object.fromEntries([
  'normal', 'lengthLive', 'indexLive', 'cardLive', 'listLive', 'commentLive',
  'pushLive', 'pushMutate', 'deferred', 'noncallable', 'nonconstructible',
].map(name => [name, Symbol(name)])));
const fields = ['pictureArray', 'commentList', 'icon', 'name', 'authorType', 'content'];
const cardFields = fields.slice(2);

async function load(source, context, initial) {
  const dependency = new vm.SyntheticModule(Object.keys(initial), function() {
    for (const [key, value] of Object.entries(initial)) this.setExport(key, value);
  }, {context});
  const root = new vm.SourceTextModule(source, {context});
  await root.link(name => {
    assert.equal(name, '@normalized:N&&&base/Index&1.0.0'); return dependency;
  });
  assert.throws(() => root.namespace.CardItem, {name: 'ReferenceError'});
  await root.evaluate();
  assert.deepEqual(Object.keys(root.namespace), ['CardItem']);
  const C = root.namespace.CardItem;
  assert.equal(C.name, 'CardItem'); assert.equal(C.length, 2);
  assert.deepEqual(Reflect.ownKeys(C.prototype), ['constructor']);
  assert.throws(() => Reflect.apply(C, {}, []), {name: 'TypeError'});
  return {C, dependency};
}

function poison(fn) {
  for (const key of ['call', 'apply', 'bind']) Object.defineProperty(fn, key, {get() {
    assert.fail(`unexpected ${key} lookup`);
  }});
  return fn;
}

// Execute the complete real module and independent TS module. Ordinary own-field
// behavior is compared here; inherited initialization setters are a separate
// pinned assignment probe because Node TS fields use DefineField.
async function observe(source, mode, input = 1, size = 2, fault = null, failure) {
  const context = vm.createContext({}), trace = [], callbacks = [], arrays = [], ids = new WeakMap();
  const tokens = [{}, Symbol('name'), -0, NaN], comments = [{}, Symbol('comment')];
  const pictures = [{}, Symbol('picture')];
  let dependency, instance, inject = true, raised = false, thrown, constructing = true;
  const event = (label, ...values) => {
    trace.push([label, ...values]); if (inject && trace.length - 1 === fault) throw failure;
  };
  const arrayId = value => {
    assert.ok(Array.isArray(value));
    if (!ids.has(value)) { ids.set(value, arrays.length); arrays.push(value); }
    return ids.get(value);
  };
  const arrayPrototype = vm.runInContext('Array.prototype', context), push = arrayPrototype.push;
  const pushCall = label => poison(function(value) {
    assert.equal(arguments.length, 1); event(`push:${label}:call`, arrayId(this), value.label, comments.indexOf(value.item));
    return Reflect.apply(push, this, [value]);
  });
  Object.defineProperty(arrayPrototype, 'push', {configurable: true, get() {
    event('push:get', arrayId(this));
    if (constructing && mode === Mode.pushLive) dependency.setExport('CommentItem', lateComment);
    return pushCall('early');
  }});
  const constructor = label => poison(function CommentItem(item) {
    assert.equal(new.target, CommentItem); assert.equal(arguments.length, 1);
    assert.ok(comments.includes(item)); event(`comment:${label}`, comments.indexOf(item));
    if (constructing && mode === Mode.commentLive) dependency.setExport('CommentItem', lateComment);
    if (constructing && mode === Mode.pushMutate) Object.defineProperty(arrays.at(-1), 'push', {
      value: pushCall('late'), configurable: true, writable: true,
    });
    this.item = item; this.label = label;
  });
  const earlyComment = constructor('early'), lateComment = constructor('late');
  const number = (value, label) => value === 'coercible' ? {[Symbol.toPrimitive](hint) {
    assert.equal(hint, 'number'); event(`${label}:coerce`);
    if (label === 'index' && mode === Mode.indexLive) dependency.setExport('PICTURE_ARRAY_LIST', latePictures);
    return 1;
  }} : value;
  const pictureList = label => new Proxy({0: pictures[0], 1: pictures[1]}, {get(target, key, receiver) {
    event(`pictures:${label}:${String(key)}`);
    if (key === 'length') {
      if (constructing && mode === Mode.lengthLive) dependency.setExport('PICTURE_ARRAY_LIST', latePictures);
      return number(size, 'length');
    }
    return Reflect.get(target, key, receiver);
  }});
  const earlyPictures = pictureList('early'), latePictures = pictureList('late');
  const list = label => {
    const owner = {};
    Object.defineProperty(owner, 'forEach', {get() {
      event(`list:${label}:get`);
      if (constructing && mode === Mode.listLive) dependency.setExport('COMMENT_LIST', lateList);
      if (mode === Mode.noncallable) return null;
      return poison(function(callback) {
        assert.equal(this, owner); assert.equal(arguments.length, 1);
        assert.equal(callback.name, ''); assert.equal(callback.length, 1);
        assert.throws(() => Reflect.construct(callback, []), {name: 'TypeError'});
        callbacks.push(callback); event(`list:${label}:call`);
        if (mode !== Mode.deferred) for (const item of comments)
          Reflect.apply(callback, {commentList: 'foreign receiver'}, [item, 'ignored']);
      });
    }});
    return owner;
  };
  const earlyList = list('early'), lateList = list('late'), card = {};
  for (const [slot, key] of cardFields.entries()) Object.defineProperty(card, key, {get() {
    assert.equal(this, card); event(`card:${key}`);
    if (constructing && mode === Mode.cardLive) dependency.setExport('PICTURE_ARRAY_LIST', latePictures);
    return tokens[slot];
  }});
  const loaded = await load(source, context, {COMMENT_LIST: earlyList,
    CommentItem: mode === Mode.nonconstructible ? () => {} : earlyComment, PICTURE_ARRAY_LIST: earlyPictures});
  dependency = loaded.dependency;
  try {
    instance = new loaded.C(card, number(input, 'index'), 'ignored');
    assert.equal(Object.getPrototypeOf(instance), loaded.C.prototype);
    assert.deepEqual(Reflect.ownKeys(instance), fields);
    for (const key of fields) {
      const d = Object.getOwnPropertyDescriptor(instance, key);
      assert.deepEqual([d.writable, d.enumerable, d.configurable], [true, true, true]);
    }
    for (const [slot, key] of cardFields.entries()) assert.equal(instance[key], tokens[slot]);
    assert.ok(pictures.includes(instance.pictureArray) || instance.pictureArray === undefined);
  } catch (error) { raised = true; thrown = error; }
  if (fault !== null) { assert.equal(raised, true); assert.equal(thrown, failure); }
  else if (raised) {
    assert.equal(thrown?.name, 'TypeError');
    assert.ok(mode === Mode.noncallable || mode === Mode.nonconstructible || typeof input === 'symbol' ||
      (typeof input === 'bigint') !== (typeof size === 'bigint') || typeof size === 'symbol');
  }
  const points = trace.length;
  // Retained callbacks must keep the original activation even after a failed
  // constructor. A successful instance may replace its field before replay.
  inject = false; constructing = false;
  if (callbacks.length) {
    dependency.setExport('CommentItem', lateComment);
    if (instance) instance.commentList = vm.runInContext('[]', context);
    for (const callback of callbacks) Reflect.apply(callback, {commentList: 'wrong after return'}, [comments[1]]);
  }
  const state = arrays.map(array => ({keys: Object.keys(array),
    items: Array.from(array, item => [item.label, comments.indexOf(item.item)])}));
  return {trace, points, raised, state,
    picture: instance ? pictures.indexOf(instance.pictureArray) : null};
}

const Prototype = Object.freeze({class: Symbol('class'), foreign: Symbol('foreign'),
  fallback: Symbol('fallback'), foreignFallback: Symbol('foreign-fallback')});

async function assignment(source, kind, fault = null, failure) {
  const context = vm.createContext({}), trace = [], writes = [], store = new Map(), arrays = [];
  const tokens = [{}, Symbol('name'), -0, NaN], item = {}, picture = {};
  let receiver, returned, raised = false, thrown;
  const event = label => { trace.push(label); if (label === fault) throw failure; };
  const arrayPrototype = vm.runInContext('Array.prototype', context), push = arrayPrototype.push;
  Object.defineProperty(arrayPrototype, 'push', {configurable: true, get() {
    assert.equal(this, store.get('commentList')); event('push:get');
    return poison(function(value) {
      assert.equal(this, store.get('commentList')); assert.equal(arguments.length, 1);
      assert.equal(value.item, item); event('push:call'); return Reflect.apply(push, this, [value]);
    });
  }});
  function CommentItem(value) {
    assert.equal(new.target, CommentItem); assert.equal(value, item);
    event('comment:construct'); this.item = value;
  }
  const comments = {};
  Object.defineProperty(comments, 'forEach', {get() {
    event('comments:get'); return poison(function(callback) {
      assert.equal(this, comments); event('comments:call');
      Reflect.apply(callback, {commentList: 'wrong'}, [item]);
    });
  }});
  const pictures = new Proxy({}, {get(_, key) {
    if (key === 'length') {
      event('pictures:length'); return {[Symbol.toPrimitive](hint) {
        assert.equal(hint, 'number'); event('length:coerce'); return 2;
      }};
    }
    assert.equal(key, '1'); event('pictures:1'); return picture;
  }});
  const index = {[Symbol.toPrimitive](hint) {
    assert.equal(hint, 'number'); event('index:coerce'); return 3;
  }};
  const card = {};
  for (const [slot, key] of cardFields.entries()) Object.defineProperty(card, key, {get() {
    assert.equal(this, card); event(`card:${key}`); return tokens[slot];
  }});
  const {C} = await load(source, context, {COMMENT_LIST: comments, CommentItem, PICTURE_ARRAY_LIST: pictures});
  const targetContext = kind === Prototype.foreignFallback ? vm.createContext({}) : context;
  const fallback = kind === Prototype.fallback || kind === Prototype.foreignFallback;
  const prototype = fallback ? vm.runInContext('Object.prototype', targetContext) : kind === Prototype.class ? C.prototype : {};
  for (const key of fields) Object.defineProperty(prototype, key, {configurable: true,
    set(value) {
      if (receiver) assert.equal(this, receiver); receiver = this;
      const label = key === 'pictureArray' ? `set:${key}:${writes.filter(w => w.key === key).length}` : `set:${key}`;
      event(label);
      if (Array.isArray(value)) {
        assert.equal(value.length, 0); assert.equal(Object.getPrototypeOf(value), arrayPrototype);
        assert.ok(!arrays.includes(value)); arrays.push(value);
      } else assert.equal(value, key === 'pictureArray' ? picture : tokens[cardFields.indexOf(key)]);
      writes.push({key, value}); store.set(key, value);
    },
    get() { assert.equal(this, receiver); event(`get:${key}`); return store.get(key); },
  });
  const Target = vm.runInContext('(function Target() {})', targetContext);
  const target = new Proxy(Target, {get(object, key, actual) {
    if (key === 'prototype') { event('prototype'); return fallback ? 0 : prototype; }
    return Reflect.get(object, key, actual);
  }});
  try { returned = Reflect.construct(C, [card, index], target); }
  catch (error) { raised = true; thrown = error; }
  if (fault) { assert.equal(raised, true); assert.equal(thrown, failure); }
  else { assert.equal(raised, false); assert.equal(returned, receiver); }
  // Independent pinned assignment/evaluation-order contract, not the emitted
  // program rewritten as an oracle. Setter throws must preserve prior writes.
  const expected = ['prototype', 'set:pictureArray:0', 'set:commentList',
    ...cardFields.flatMap(key => [`card:${key}`, `set:${key}`]), 'pictures:length',
    'index:coerce', 'length:coerce', 'pictures:1', 'set:pictureArray:1',
    'comments:get', 'comments:call', 'get:commentList', 'push:get', 'comment:construct', 'push:call'];
  const prefix = fault ? expected.slice(0, expected.indexOf(fault) + 1) : expected;
  assert.deepEqual(trace, prefix);
  const completed = fault ? prefix.slice(0, -1) : prefix;
  assert.deepEqual(writes.map(w => w.key), completed.filter(label => label.startsWith('set:')).map(label => label.split(':')[1]));
  if (receiver) {
    assert.equal(Object.getPrototypeOf(receiver), prototype); assert.deepEqual(Reflect.ownKeys(receiver), []);
  }
  if (store.has('commentList')) assert.equal(store.get('commentList').length, completed.includes('push:call') ? 1 : 0);
  return expected;
}

export async function verifyCardItemState(source, reference) {
  let cardItemCases = 0, cardItemThrows = 0, cardItemAssignments = 0, cardItemMutations = 0;
  for (const mode of Object.values(Mode)) {
    const input = mode === Mode.indexLive ? 'coercible' : 1;
    const baseline = await observe(source, mode, input);
    assert.deepEqual(baseline, await observe(reference, mode, input)); ++cardItemCases;
    for (let fault = 0; fault < baseline.points; ++fault) for (const failure of [undefined, 0, Symbol('failure'), {}]) {
      assert.deepEqual(await observe(source, mode, input, 2, fault, failure),
        await observe(reference, mode, input, 2, fault, failure)); ++cardItemThrows;
    }
  }
  for (const input of [undefined, null, false, true, -0, -1, 0, 1, 3, NaN, Infinity, '1', 'coercible', 3n, Symbol('index')])
    for (const size of [0, 2, -1, NaN, Infinity, '2', 'coercible', 2n, Symbol('length')]) {
      assert.deepEqual(await observe(source, Mode.normal, input, size),
        await observe(reference, Mode.normal, input, size)); ++cardItemCases;
    }
  for (const kind of Object.values(Prototype)) {
    const points = await assignment(source, kind); ++cardItemAssignments;
    for (const point of points) for (const failure of [undefined, 0, Symbol('failure'), {}]) {
      await assignment(source, kind, point, failure); ++cardItemAssignments;
    }
  }
  const baseline = await observe(reference, Mode.normal);
  for (const changed of [source.replace('card.icon', 'card.name'),
    source.replace('index % PICTURE_ARRAY_LIST.length', '0'),
    source.replace('new CommentItem(item)', 'new CommentItem(({}))'),
    source.replace(/\(item\) => \{/, 'function(item) {'),
    source.replace('return $local2;', 'return {};'),
    source.replace('$local2.pictureArray = [];', 'Object.defineProperty($local2, "pictureArray", {value: [], writable: true, configurable: true, enumerable: true});'),
  ]) {
    assert.notEqual(changed, source); new vm.SourceTextModule(changed);
    await assert.rejects((async () => {
      assert.deepEqual(await observe(changed, Mode.normal), baseline);
      await assignment(changed, Prototype.foreign);
    })(), {name: 'AssertionError'}); ++cardItemMutations;
  }
  return {cardItemCases, cardItemThrows, cardItemAssignments, cardItemMutations};
}

const PeerMode = Object.freeze({normal: Symbol('normal'), reverse: Symbol('reverse'), deferred: Symbol('deferred')});

// Both constructors below are real emitted modules. The controlled barrel only
// reexports the actual CommentItem and supplies the two input lists; no model
// constructor, callback or generated body is replaced for this comparison.
async function peers(source, commentSource, mode, count, review, sub, fault = null, failure) {
  const context = vm.createContext({}), trace = [], callbacks = [], arrays = [], ids = new WeakMap();
  const keys = ['icon', 'name', 'favorCount', 'content', 'timePosition', 'reviewCnt', 'subContent'];
  const values = keys.map(() => ({})), pictures = [{}];
  let inject = true, instance, raised = false, thrown;
  const event = label => { trace.push(label); if (inject && trace.length - 1 === fault) throw failure; };
  const arrayId = value => {
    assert.ok(Array.isArray(value));
    if (!ids.has(value)) { ids.set(value, arrays.length); arrays.push(value); }
    return ids.get(value);
  };
  const arrayPrototype = vm.runInContext('Array.prototype', context), push = arrayPrototype.push;
  Object.defineProperty(arrayPrototype, 'push', {configurable: true, get() {
    arrayId(this); event('push:get'); return poison(function(value) {
      assert.ok(value instanceof comment.namespace.CommentItem);
      event('push:call'); return Reflect.apply(push, this, [value]);
    });
  }});
  const rows = Array.from({length: Math.max(1, count)}, (_, index) => {
    const row = {}, accesses = new Map();
    for (const [slot, key] of keys.entries()) Object.defineProperty(row, key, {get() {
      assert.equal(this, row); const n = (accesses.get(key) ?? 0) + 1; accesses.set(key, n);
      event(`row:${index}:${key}:${n}`);
      return key === 'reviewCnt' && n % 2 === 1 ? review : key === 'subContent' && n % 2 === 1 ? sub : values[slot];
    }});
    return row;
  });
  const list = {forEach(callback) {
    assert.equal(this, list); assert.equal(arguments.length, 1);
    assert.equal(callback.name, ''); assert.equal(callback.length, 1);
    assert.throws(() => Reflect.construct(callback, []), {name: 'TypeError'});
    callbacks.push(callback); event('comments');
    if (mode !== PeerMode.deferred) for (const row of mode === PeerMode.reverse ? rows.slice(0, count).reverse() : rows.slice(0, count))
      Reflect.apply(callback, {commentList: 'wrong constructor receiver'}, [row]);
  }};
  const input = new vm.SyntheticModule(['COMMENT_LIST', 'PICTURE_ARRAY_LIST'], function() {
    this.setExport('COMMENT_LIST', list); this.setExport('PICTURE_ARRAY_LIST', pictures);
  }, {context});
  const comment = new vm.SourceTextModule(commentSource, {context});
  const barrel = new vm.SourceTextModule('export { CommentItem } from "comment"; export { COMMENT_LIST, PICTURE_ARRAY_LIST } from "inputs";', {context});
  const root = new vm.SourceTextModule(source, {context});
  await root.link(name => {
    if (name === 'comment') return comment;
    if (name === 'inputs') return input;
    assert.equal(name, '@normalized:N&&&base/Index&1.0.0'); return barrel;
  });
  assert.throws(() => root.namespace.CardItem, {name: 'ReferenceError'});
  assert.throws(() => comment.namespace.CommentItem, {name: 'ReferenceError'});
  await root.evaluate();
  assert.deepEqual(Object.keys(comment.namespace), ['CommentItem']);
  const card = Object.fromEntries(cardFields.map(key => [key, {}]));
  try {
    instance = new root.namespace.CardItem(card, 0);
    assert.equal(instance.pictureArray, pictures[0]);
    if (mode === PeerMode.deferred) for (const callback of callbacks) for (const row of rows.slice(0, count))
      Reflect.apply(callback, {commentList: 'wrong delayed receiver'}, [row]);
  } catch (error) { raised = true; thrown = error; }
  if (fault !== null) { assert.equal(raised, true); assert.equal(thrown, failure); }
  else assert.equal(raised, false);
  if (instance) arrayId(instance.commentList);
  const state = () => arrays.map(array => Array.from(array, item => {
    assert.equal(Object.getPrototypeOf(item), comment.namespace.CommentItem.prototype);
    return Reflect.ownKeys(item).map(key => {
      const d = Object.getOwnPropertyDescriptor(item, key);
      assert.deepEqual([d.writable, d.enumerable, d.configurable], [true, true, true]);
      const slot = keys.indexOf(key); assert.ok(slot >= 0);
      if (d.value === values[slot]) return [key, 'field'];
      assert.ok(key === 'reviewCnt' || key === 'subContent');
      assert.equal(d.value, key === 'reviewCnt' ? review : sub);
      return [key, 'input'];
    });
  }));
  const points = trace.length, before = state();
  inject = false;
  if (instance) instance.commentList = vm.runInContext('[]', context);
  for (const callback of callbacks) Reflect.apply(callback, {commentList: 'wrong recovery receiver'}, [rows[0]]);
  const after = state();
  await root.evaluate(); assert.equal(root.namespace.CardItem.name, 'CardItem');
  return {trace, points, raised, before, after};
}

export async function verifyCardItemPeers(source, comment, reference, commentReference) {
  let cardItemPeerCases = 0, cardItemPeerThrows = 0, cardItemPeerMutations = 0;
  for (const mode of Object.values(PeerMode)) {
    const baseline = await peers(source, comment, mode, 2, true, true);
    assert.deepEqual(baseline, await peers(reference, commentReference, mode, 2, true, true)); ++cardItemPeerCases;
    for (let fault = 0; fault < baseline.points; ++fault) for (const failure of [undefined, 0, Symbol('failure'), {}]) {
      assert.deepEqual(await peers(source, comment, mode, 2, true, true, fault, failure),
        await peers(reference, commentReference, mode, 2, true, true, fault, failure)); ++cardItemPeerThrows;
    }
  }
  const conditions = [undefined, null, false, true, -0, NaN, '', 'value', 0n, 1n, Symbol('condition'), {}];
  for (const review of conditions) for (const sub of conditions) {
    assert.deepEqual(await peers(source, comment, PeerMode.normal, 1, review, sub),
      await peers(reference, commentReference, PeerMode.normal, 1, review, sub)); ++cardItemPeerCases;
  }
  for (const count of [0, 1, 2, 200, 201]) {
    assert.deepEqual(await peers(source, comment, PeerMode.normal, count, true, true),
      await peers(reference, commentReference, PeerMode.normal, count, true, true)); ++cardItemPeerCases;
  }
  const baseline = await peers(reference, commentReference, PeerMode.normal, 2, true, true);
  for (const changed of [comment.replace('this.icon = item.icon', 'this.icon = item.name'),
    comment.replace('if (item?.reviewCnt)', 'if (false)'),
    comment.replace('this.subContent = item.subContent', 'this.subContent = item?.subContent ? true : false'),
  ]) {
    assert.notEqual(changed, comment); new vm.SourceTextModule(changed);
    await assert.rejects(peers(source, changed, PeerMode.normal, 2, true, true)
      .then(actual => assert.deepEqual(actual, baseline)), {name: 'AssertionError'}); ++cardItemPeerMutations;
  }
  return {cardItemPeerCases, cardItemPeerThrows, cardItemPeerMutations};
}
