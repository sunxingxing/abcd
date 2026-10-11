import assert from 'node:assert/strict';
import vm from 'node:vm';
import {verifySignatures} from './signature_shapes.mjs';

const reference = `class Subject {
  constructor(value = -1, other = undefined) { this.result = [value, other]; }
  method(value = 2, ...tail) { return [value, ...tail]; }
  async work(value = 3) { return value; }
  *stream(value = 4) { yield value; }
}
function builder(parent = null) { return parent; }`;
const equivalent = reference.replaceAll('value', 'renamed').replace('other = undefined', 'other = void 0');
verifySignatures(equivalent, reference);
verifySignatures(reference.replace('method(value', '"method"(value'), reference);
verifySignatures('class Base { constructor() {} } class Derived extends Base { constructor(...xs) { super(...xs); } }',
  'class Base {} class Derived extends Base {}');
verifySignatures('let helper; helper = function(renamed = null) {};', 'function helper(value = null) {}');
verifySignatures('function renamed(x = null) {}', 'function helper(value = null) {}');
verifySignatures('function f(undefined, x = undefined) {}', 'function f(first, second = first) {}');
assert.throws(() => verifySignatures('function f(undefined, x = void 0) {}', 'function f(undefined, x = undefined) {}'));
assert.throws(() => verifySignatures('function f(x, y = parameter0) {}', 'function f(x, y = x) {}'));
assert.throws(() => verifySignatures('let undefined = 7; function f(x = undefined) {}', 'let undefined = 7; function f(x = void 0) {}'));

const changes = [
  ['value = -1', 'value = 1'], ['other = undefined', 'other = null'],
  ['value = -1', 'value'], ['...tail', 'tail'], ['async work', 'work'],
  ['*stream', 'stream'], ['parent = null', 'parent = false'],
];
let mutations = 0;
for (const [before, after] of changes) {
  let wrong = reference.replace(before, after);
  if (before === '*stream') wrong = wrong.replace('yield value', 'return value');
  assert.throws(() => verifySignatures(wrong, reference), {name: 'AssertionError'});
  // Independently execute valid mutated code. Each rejected signature also has
  // an observable difference; this is not a printer-text snapshot assertion.
  const observe = async text => {
    const {Subject, builder} = vm.runInNewContext(text + '; ({Subject, builder})');
    const instance = new Subject();
    const stream = instance.stream(), work = instance.work();
    let call;
    try {call = instance.method(undefined, 'x', 'y');} catch (error) {call = error.name;}
    return JSON.stringify({result: instance.result, length: Subject.length, call, builder: builder(),
      workAsync: typeof work?.then === 'function', work: await work,
      stream: typeof stream?.next === 'function' ? stream.next() : stream},
      (_key, value) => value === undefined ? {undefined: true} : value);
  };
  assert.notEqual(await observe(wrong), await observe(reference));
  ++mutations;
}
assert.throws(() => verifySignatures('class Subject {}', reference));
assert.throws(() => verifySignatures('function f({x}) {}', 'function f({x}) {}'), /destructuring/);
assert.throws(() => verifySignatures('function f(x = () => 1) {}', 'function f(x = () => 1) {}'), /nested default/);
console.log(JSON.stringify({mutations, independentExecution: true, alphaRenaming: true, scopeCounterexample: true}));
