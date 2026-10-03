import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const poison = {poison: true};
function load(option) {
  const source = execFileSync(process.argv[2], [option], {encoding: 'utf8'});
  assert.ok(source.includes('new ') && source.includes('constructor('));
  assert.ok(!source.includes('...') && !source.includes('Reflect.') && !source.includes('.apply('));
  const context = vm.createContext({poison});
  vm.runInContext(`
    for (const key of ['0', '1']) Object.defineProperty(Array.prototype, key, {
      get() { throw poison; }, set() { throw poison; }, configurable: true
    });
    Array.prototype[Symbol.iterator] = function() { throw poison; };
    Reflect.construct = function() { throw poison; };
    Array = function() { throw poison; };
  `, context);
  return vm.runInContext(source, context);
}
const early = load('--computed-source'), late = load('--late-source');
const caught = load('--caught-source'), lateCaught = load('--late-caught-source');
const empty = load('--empty-source'), buffer = load('--buffer-source');
const payloads = [undefined, null, false, true, 0, -0, 1, NaN, Infinity, '', 'text',
  0n, 1n, Symbol('identity'), {}, [], () => {}];
const reflectConstruct = Reflect.construct;
// The constructor is recovered from actual class-creation bytecode and a
// prepared constructor body returning its first parameter. This oracle copies
// the pinned array indices, independent of recovered syntax or an iterator.
function reference(provider, input, isLate, catches) {
  const Known = class Known {constructor(first, second) {return first;}};
  const run = () => {
    if (!isLate) provider.ctor;
    const args = [];
    for (let a = 0; a < 2; ++a) Object.defineProperty(args, String(a), {
      value: input.value, writable: true, enumerable: true, configurable: true,
    });
    if (isLate) provider.ctor;
    const length = args.length, copied = [];
    for (let a = 0; a < length; ++a) copied[a] = args[a];
    return reflectConstruct(Known, copied, Known);
  };
  if (!catches) return run();
  try {return run();} catch (error) {return error;}
}
function scenario(fn, first, second, fail, isLate, catches) {
  const trace = []; let reads = 0;
  // This getter returns a deliberately unsafe unknown constructor value. The
  // bytecode observes it but constructs its separately created class identity.
  const unknown = new Proxy(() => {}, {get() {assert.fail('unknown value used as target');}});
  const provider = {get ctor() {trace.push(['ctor']); if (fail === 1) throw first; return unknown;}};
  const input = {get value() {
    trace.push(['value', reads]);
    if (fail === reads + 2) throw first;
    return reads++ === 0 ? first : second;
  }};
  let result, threw = false;
  try {result = fn(provider, input);} catch (error) {threw = true; result = error;}
  if (fail) {
    assert.equal(threw, !catches); assert.ok(Object.is(result, first));
  } else {
    assert.equal(threw, false);
    if (first !== null && (typeof first === 'object' || typeof first === 'function')) assert.equal(result, first);
    else {
      assert.equal(Object.getPrototypeOf(result).constructor.name, 'Known');
      assert.equal(Object.getPrototypeOf(result).constructor.length, 2);
      assert.deepEqual(Reflect.ownKeys(result), []);
    }
  }
  return trace;
}
let cases = 0;
for (const first of payloads) for (const second of payloads) for (const fail of [0, 1, 2, 3])
  for (const [fn, isLate, catches] of [[early, false, false], [late, true, false], [caught, false, true], [lateCaught, true, true]]) {
    const expected = scenario((provider, input) => reference(provider, input, isLate, catches), first, second, fail, isLate, catches);
    const actual = scenario(fn, first, second, fail, isLate, catches);
    assert.deepEqual(actual, expected); ++cases;
  }
for (const fn of [empty, buffer]) {
  const first = fn(), second = fn(); assert.notEqual(first, second);
  assert.equal(Object.getPrototypeOf(first).constructor.name, 'Known');
  assert.notEqual(Object.getPrototypeOf(first), Object.getPrototypeOf(second)); ++cases;
}
for (const option of ['--unknown-refusal', '--unknown-getter-refusal'])
  assert.equal(execFileSync(process.argv[2], [option], {encoding: 'utf8'}), '');
// Boundary model grounded in v7 NewObject -> Proxy::ConstructInternal: a proxy
// of an arrow with a construct trap can observe a trap lookup on that route.
// Native new checks IsConstructor first. This is not an Ark VM execution test.
const arrow = () => {}, token = {}, trace = [];
const handler = {get construct() {trace.push('trap'); return () => token;}};
const proxy = new Proxy(arrow, handler);
assert.throws(() => reflectConstruct(proxy, []), TypeError); assert.deepEqual(trace, []);
assert.equal(handler.construct(arrow, [], proxy), token); assert.deepEqual(trace, ['trap']);
console.log(`${cases} known-class apply argument, identity, order, exception and iterator-poison cases passed; unknown-target boundary refused`);
