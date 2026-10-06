import assert from 'node:assert/strict';
import vm from 'node:vm';

const Mode = Object.freeze({
  normal: Symbol('normal'),
  cardLive: Symbol('card-live'),
  commentLive: Symbol('comment-live'),
  lengthLive: Symbol('length-live'),
  indexLive: Symbol('index-live'),
  zeroLength: Symbol('zero-length'),
  deferredComments: Symbol('deferred-comments'),
});

// Connect two real emitted modules. Only base/Index remains a controlled host;
// CardArray actually constructs the emitted CardItem and its retained closure.
async function observe(arraySource, itemSource, mode, failedAt = null, failure) {
  const trace = [], points = new Set(), commentCallbacks = [], comments = [{}, {}];
  const tokens = [{}, {}, {}, {}], cards = [{}, {}];
  let base, swapped = false;
  const event = (name, ...values) => {
    trace.push([name, ...values]); points.add(name); if (name === failedAt) throw failure;
  };
  for (let index = 0; index < cards.length; ++index)
    for (const [slot, key] of ['icon', 'name', 'authorType', 'content'].entries())
      Object.defineProperty(cards[index], key, {get() {
        assert.equal(this, cards[index]); event(`card:${index}:${key}`);
        if (mode === Mode.cardLive) base.setExport('CARD_LIST', replacementList);
        return tokens[slot];
      }});
  const pictures = label => new Proxy(mode === Mode.zeroLength ? [] : [tokens[0], tokens[1]], {get(target, key, receiver) {
    if (key === 'length') {
      event(`pictures:${label}:length`);
      if (mode === Mode.lengthLive) base.setExport('PICTURE_ARRAY_LIST', latePictures);
    } else if (typeof key === 'string') event(`pictures:${label}:${key}`);
    return Reflect.get(target, key, receiver);
  }});
  const earlyPictures = pictures('early'), latePictures = pictures('late');
  function indexArgument(index) {
    if (mode !== Mode.indexLive) return index;
    return {[Symbol.toPrimitive](hint) {
      assert.equal(hint, 'number'); event(`index:${index}`);
      base.setExport('PICTURE_ARRAY_LIST', latePictures); return index;
    }};
  }
  const list = (label, order) => {
    const owner = {forEach(callback) {
      assert.equal(this, owner); assert.equal(callback.name, ''); assert.equal(callback.length, 2);
      assert.throws(() => Reflect.construct(callback, []), {name: 'TypeError'});
      event(`list:${label}`);
      for (const index of order) Reflect.apply(callback, {cardArray: 'wrong'}, [cards[index], indexArgument(index)]);
    }};
    return owner;
  };
  const firstList = list('early', [0, 1]), replacementList = list('late', [1, 0]);
  const constructor = label => {
    function CommentItem(item) {
      assert.equal(new.target, CommentItem); const index = comments.indexOf(item); assert.ok(index >= 0);
      event(`comment:${label}:${index}`);
      if (mode === Mode.commentLive && !swapped) { swapped = true; base.setExport('CommentItem', lateComment); }
      this.item = item; this.label = label;
    }
    return CommentItem;
  };
  const earlyComment = constructor('early'), lateComment = constructor('late');
  const commentList = {forEach(callback) {
    assert.equal(this, commentList); assert.equal(callback.name, ''); assert.equal(callback.length, 1);
    assert.throws(() => Reflect.construct(callback, []), {name: 'TypeError'});
    event('comments'); commentCallbacks.push(callback);
    if (mode !== Mode.deferredComments)
      for (const item of comments) Reflect.apply(callback, {commentList: 'wrong'}, [item]);
  }};
  const context = vm.createContext({});
  base = new vm.SyntheticModule(['CARD_LIST', 'COMMENT_LIST', 'CommentItem', 'PICTURE_ARRAY_LIST'], function() {
    this.setExport('CARD_LIST', firstList); this.setExport('COMMENT_LIST', commentList);
    this.setExport('CommentItem', earlyComment); this.setExport('PICTURE_ARRAY_LIST', earlyPictures);
  }, {context});
  const itemModule = new vm.SourceTextModule(itemSource, {context});
  const root = new vm.SourceTextModule(arraySource, {context});
  await root.link(name => {
    if (name === '@normalized:N&&&detail/src/main/ets/viewmodel/CardViewModel&1.0.0') return itemModule;
    assert.equal(name, '@normalized:N&&&base/Index&1.0.0'); return base;
  });
  assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
  assert.throws(() => itemModule.namespace.CardItem, {name: 'ReferenceError'});
  let raised = false, thrown, singleton, second;
  try {
    await root.evaluate(); singleton = root.namespace.default;
    const C = root.namespace.CardArray, I = itemModule.namespace.CardItem;
    assert.equal(I.name, 'CardItem'); assert.equal(I.length, 2);
    assert.equal(singleton.getInstance(), singleton);
    second = new C(); assert.notEqual(second.cardArray, singleton.cardArray);
    if (mode === Mode.deferredComments) {
      base.setExport('CommentItem', lateComment);
      for (const callback of commentCallbacks)
        for (const item of comments) Reflect.apply(callback, {commentList: 'wrong after construction'}, [item]);
    }
    for (const owner of [singleton, second]) for (const card of owner.cardArray) {
      assert.equal(Object.getPrototypeOf(card), I.prototype);
      assert.deepEqual(Reflect.ownKeys(card), ['pictureArray', 'commentList', 'icon', 'name', 'authorType', 'content']);
      for (const [slot, key] of ['icon', 'name', 'authorType', 'content'].entries()) {
        const field = Object.getOwnPropertyDescriptor(card, key);
        assert.equal(field.value, tokens[slot]);
        assert.deepEqual([field.writable, field.enumerable, field.configurable], [true, true, true]);
      }
      assert.equal(card.commentList.length, comments.length);
      assert.equal(Object.getPrototypeOf(card.commentList), vm.runInContext('Array.prototype', context));
    }
    assert.notEqual(singleton.cardArray[0], second.cardArray[0]);
    assert.notEqual(singleton.cardArray[0].commentList, second.cardArray[0].commentList);
    await root.evaluate(); assert.equal(root.namespace.default, singleton);
  } catch (error) { raised = true; thrown = error; }
  if (raised) {
    assert.ok(failedAt); assert.equal(thrown, failure);
    // Failure can occur in a later independent construction, after the default
    // binding initialized. Preserve that distinction rather than hiding it.
    if (!singleton) assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
    else assert.equal(root.namespace.default, singleton);
  }
  const state = owner => owner ? Array.from(owner.cardArray, card => ({
    fields: Reflect.ownKeys(card), picture: tokens.indexOf(card.pictureArray),
    comments: Array.from(card.commentList, item => [item.label, comments.indexOf(item.item)]),
  })) : null;
  return {trace, points: [...points], raised, singleton: state(singleton), second: state(second)};
}

export async function verifyConnectedCards(source, peer, reference, peerReference) {
  let connectedCases = 0, connectedThrows = 0;
  for (const mode of [Mode.normal, Mode.cardLive, Mode.commentLive, Mode.lengthLive, Mode.indexLive, Mode.zeroLength, Mode.deferredComments]) {
    const baseline = await observe(source, peer, mode);
    assert.deepEqual(baseline, await observe(reference, peerReference, mode)); ++connectedCases;
    for (const point of baseline.points) for (const failure of [undefined, 0, {}]) {
      assert.deepEqual(await observe(source, peer, mode, point, failure),
        await observe(reference, peerReference, mode, point, failure)); ++connectedThrows;
    }
  }
  const baseline = await observe(source, peer, Mode.normal);
  let connectedMutations = 0;
  for (const changed of [
    peer.replace('card.icon', 'card.name'),
    peer.replace('index % PICTURE_ARRAY_LIST.length', '0'),
    peer.replace('new CommentItem(item)', 'new CommentItem(({}))'),
  ]) {
    assert.notEqual(changed, peer); new vm.SourceTextModule(changed);
    await assert.rejects(observe(source, changed, Mode.normal).then(actual => assert.deepEqual(actual, baseline)),
      {name: 'AssertionError'}); ++connectedMutations;
  }
  return {connectedCases, connectedThrows, connectedMutations};
}
