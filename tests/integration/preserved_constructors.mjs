import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const fixtures = JSON.parse(execFileSync(process.argv[2], [], {encoding: 'utf8', timeout: 60000, maxBuffer: 8 * 1024 * 1024}));
assert.equal(fixtures.length, 72);
const references = [
  'constructor(value) { super(value); }',
  'constructor(values) { super(...values); }',
  'constructor(...values) { super(...values); }',
];
const values = [undefined, null, false, -0, NaN, 1n, Symbol('argument'), {}];
function observe(source, call, value, parentKind, behavior, alternate, iteratorFailure) {
  const trace = [], token = {}, error = {}, payload = [value, token];
  let C, requested;
  const label = item => item === token ? 'token' : Object.is(item, value) ? 'value' : typeof item;
  function Base(...args) {
    trace.push(['base', args.map(label), new.target === requested]);
    if (behavior === 'throw') throw error;
    if (behavior === 'object') return token;
    this.mark = token;
    return 5;
  }
  const wrap = (target, name) => new Proxy(target, {
    construct(target, args, receiver) {
      trace.push([name, args.map(label), receiver === requested]);
      return Reflect.construct(target, args, receiver);
    },
  });
  const original = parentKind === 'bound' ? Base.bind(null, token) : Base;
  const parent = parentKind === 'proxy' ? wrap(original, 'proxy') : Base;
  // This wrapper supplies the actual class grammar required by the emitted,
  // file-backed constructor receipt. Its body is exclusively engine output.
  C = Function('Base', `return class extends Base { ${source} };`)(parent);
  if (parentKind === 'bound') Object.setPrototypeOf(C, original);
  if (parentKind === 'changed') Object.setPrototypeOf(C, wrap(Base, 'changed'));
  if (parentKind === 'invalid') Object.setPrototypeOf(C, null);
  requested = alternate ? function Alternate() {} : C;
  let args = call === 0 ? [value, token] : call === 2 ? payload : [payload];
  if (call === 1 && iteratorFailure !== 'none') {
    args = [{get [Symbol.iterator]() {
      trace.push(['iterator']);
      if (iteratorFailure === 'iterator') throw error;
      return function () {
        trace.push(['acquire']);
        if (iteratorFailure === 'replace') Object.setPrototypeOf(C, wrap(Base, 'late'));
        let i = 0;
        return {next() {
          trace.push(['next', i]);
          if (iteratorFailure === 'next') throw error;
          const current = i++;
          return {get done() {
            trace.push(['done', current]);
            if (iteratorFailure === 'done') throw error;
            return current === payload.length;
          }, get value() {
            trace.push(['value', current]);
            if (iteratorFailure === 'value') throw error;
            return payload[current];
          }};
        }, return() {trace.push(['close']); return {};}};
      };
    }}];
  }
  let completion;
  try {
    const result = Reflect.construct(C, args, requested);
    completion = ['return', result === token ? 'token' : result.mark === token ? 'instance' : 'other',
      result === token || Object.getPrototypeOf(result) === requested.prototype];
  } catch (thrown) {completion = ['throw', thrown === error ? 'original' : thrown.name];}
  return {trace, completion};
}
let comparisons = 0;
for (const {call, source} of fixtures) {
  assert.doesNotMatch(source, /INCOMPLETE|UNRECOVERED/);
  for (const value of values) for (const parent of ['ordinary', 'proxy', 'bound', 'changed', 'invalid'])
    for (const behavior of ['primitive', 'object', 'throw']) for (const alternate of [false, true])
      for (const iterator of call === 1 ? ['none', 'iterator', 'next', 'done', 'value', 'replace'] : ['none']) {
        assert.deepEqual(observe(source, call, value, parent, behavior, alternate, iterator),
          observe(references[call], call, value, parent, behavior, alternate, iterator));
        ++comparisons;
      }
}
// Verify the comparison catches lost arguments and missing construction.
assert.notDeepEqual(observe('constructor(value) { super(); }', 0, 17, 'proxy', 'primitive', false, 'none'),
  observe(references[0], 0, 17, 'proxy', 'primitive', false, 'none'));
assert.notDeepEqual(observe('constructor(value) { return {}; }', 0, 17, 'proxy', 'primitive', false, 'none'),
  observe(references[0], 0, 17, 'proxy', 'primitive', false, 'none'));
console.log(JSON.stringify({preservedConstructors: fixtures.length, comparisons, counterexamples: 2}));
