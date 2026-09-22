import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const source = await import(pathToFileURL(process.argv[2]));
const {method0} = source;
const receiver = {method() { return this; }};
assert.equal(method0(receiver), receiver);
const a = Object.freeze({a: 1}), b = Symbol('b'), c = 42n;
const expected = [[], [a], [b, a], [a, a, c]];
for (const prefix of ['method', 'store']) {
  for (let arity = 0; arity < 4; ++arity) {
    const events = [];
    const callable = function(...args) {
      events.push('call');
      assert.equal(this, object);
      assert.equal(new.target, undefined);
      assert.deepEqual(args, expected[arity]);
      return args;
    };
    Object.defineProperty(callable, 'call', {get() { throw new Error('call lookup'); }});
    Object.defineProperty(callable, 'apply', {get() { throw new Error('apply lookup'); }});
    const object = new Proxy({}, {
      get(target, key, received) {
        assert.equal(key, 'method');
        assert.equal(received, object);
        events.push('get');
        return callable;
      },
    });
    assert.deepEqual(source[prefix + arity](object, a, b, c), expected[arity]);
    assert.deepEqual(events, ['get', 'call']);
  }
}
const failure = Object.freeze({error: 'identity'});
assert.throws(() => method0({get method() { throw failure; }}), error => error === failure);
assert.throws(() => method0({method() { throw failure; }}), error => error === failure);
assert.throws(() => method0({method: class Method {}}), TypeError);
assert.throws(() => method0({method: 42}), TypeError);
assert.throws(() => method0(null), TypeError);
assert.throws(() => method0(undefined), TypeError);
let reads = 0;
const changing = {
  get method() {
    ++reads;
    Object.defineProperty(this, 'method', {value() { throw new Error('second lookup'); }});
    return function() { return this; };
  },
};
assert.equal(method0(changing), changing);
assert.equal(reads, 1);
let applies = 0;
const proxyReceiver = {
  method: new Proxy(function() {}, {
    apply(target, receiver, args) {
      ++applies;
      assert.equal(receiver, proxyReceiver);
      assert.deepEqual(args, [a, a, c]);
      return b;
    },
  }),
};
assert.equal(source.method3(proxyReceiver, a, b, c), b);
assert.equal(applies, 1);
let loops = 0;
assert.equal(source.loop({method() { return ++loops < 5; }}), false);
assert.equal(loops, 5);
for (const primitive of [false, 17, 'text', 42n, Symbol('receiver')]) {
  const prototype = Object.getPrototypeOf(primitive);
  const saved = Object.getOwnPropertyDescriptor(prototype, 'method');
  let calls = 0;
  Object.defineProperty(prototype, 'method', {
    configurable: true,
    get() {
      assert.equal(this, primitive);
      ++calls;
      return function() { return this; };
    },
  });
  try {
    assert.equal(method0(primitive), primitive);
    assert.equal(calls, 1);
  } finally {
    if (saved) Object.defineProperty(prototype, 'method', saved);
    else delete prototype.method;
  }
}
