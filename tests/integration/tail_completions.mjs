import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const reports = execFileSync(process.argv[2], ['--tail-completions'], {encoding: 'utf8'})
  .trim().split('\n').map(JSON.parse);
assert.equal(reports.length, 54);
const asyncKind = kind => kind.startsWith('async-');
const generatorKind = kind => kind.endsWith('generator');
function reference(kind, scenario) {
  const bodies = {
    bare: 'return;', literal: 'return void 0;', value: 'return value;', call: 'return event();',
    sequence: 'return (event(), void 0);', early: 'return void 0; event();',
    nested: '{ return void 0; } event();',
    'finally-return': 'try { return value; } finally { return void 0; }',
    suspend: `${generatorKind(kind) ? 'yield value;' : asyncKind(kind) ? 'await value;' : 'value;'} return void 0;`
  };
  const prefix = {
    function: 'function', arrow: '', 'async-function': 'async function', 'async-arrow': 'async',
    generator: 'function*', 'async-generator': 'async function*'
  }[kind];
  return `(${prefix}(event, value) ${kind.endsWith('arrow') ? '=>' : ''} { ${bodies[scenario]} })`;
}
async function observe(source, kind, scenario, input, throwing, request) {
  const fn = vm.runInNewContext(source), trace = [], failure = Symbol('arbitrary failure');
  assert.equal(fn.name, ''); assert.equal(fn.length, 2);
  const token = {}, symbol = Symbol('value');
  const values = {undefined: undefined, null: null, zero: -0, nan: NaN, number: 42,
    bigint: 7n, symbol, object: token};
  const value = values[input];
  function normalize(result) {
    if (result === token) return 'input-object';
    if (result === symbol) return 'input-symbol';
    return result;
  }
  const event = () => { trace.push('event'); if (throwing) throw failure; return value; };
  const outcome = action => {
    try { return {value: action()}; } catch (error) { assert.equal(error, failure); return {error: 'failure'}; }
  };
  const settle = async promise => {
    try { return {value: await promise}; } catch (error) { assert.equal(error, failure); return {error: 'failure'}; }
  };
  let result;
  if (generatorKind(kind)) {
    const iterator = fn(event, value), results = [];
    const step = async action => {
      const result = asyncKind(kind) ? await settle(action()) : outcome(action);
      return result.error ? result : {value: normalize(result.value.value), done: result.value.done};
    };
    results.push(await step(() => iterator.next()));
    if (!results[0].error && !results[0].done) results.push(await step(() =>
      request === 'return' ? iterator.return(71) : request === 'throw' ? iterator.throw(failure) : iterator.next(93)));
    results.push(await step(() => iterator.next()));
    result = results;
  } else {
    result = asyncKind(kind) ? await settle(fn(event, value)) : outcome(() => fn(event, value));
    if (!result.error) result.value = normalize(result.value);
  }
  if (scenario === 'early' || scenario === 'nested' || scenario === 'finally-return') assert.deepEqual(trace, []);
  return {trace, result};
}
let cases = 0;
for (const report of reports) {
  if (report.omitted) assert.doesNotMatch(report.source, /\breturn\b/);
  else assert.match(report.source, /\breturn\b/);
  for (const input of ['undefined', 'null', 'zero', 'nan', 'number', 'bigint', 'symbol', 'object'])
    for (const throwing of [false, true]) for (const request of ['next', 'return', 'throw']) {
      assert.deepEqual(await observe(report.source, report.callable, report.case, input, throwing, request),
        await observe(reference(report.callable, report.case), report.callable, report.case, input, throwing, request));
      ++cases;
    }
}
async function queued(source) {
  const fn = vm.runInNewContext(source), trace = [], failure = Symbol('queued throw');
  let release;
  const value = new Promise(resolve => { release = resolve; });
  const iterator = fn(() => { throw new Error('unexpected event'); }, value);
  const observe = (label, promise) => promise.then(result => {
    trace.push([label, result.value, result.done]);
  }, error => { assert.equal(error, failure); trace.push([label, 'throw']); });
  const pending = [observe('first', iterator.next()), observe('second', iterator.next()),
    observe('return', iterator.return(17)), observe('throw', iterator.throw(failure)), observe('last', iterator.next())];
  await Promise.resolve(); trace.push(['before-release']);
  release(42); await Promise.all(pending);
  return trace;
}
const suspended = reports.find(row => row.callable === 'async-generator' && row.case === 'suspend');
assert.deepEqual(await queued(suspended.source), await queued(reference('async-generator', 'suspend')));
async function completionOrder(source, kind) {
  const fn = vm.runInNewContext(source), trace = [];
  const result = generatorKind(kind) ? fn(() => {}, undefined).next() : fn(() => {}, undefined);
  const done = result.then(() => { trace.push('completion'); });
  const ticks = Promise.resolve().then(() => { trace.push('tick-1'); })
    .then(() => { trace.push('tick-2'); });
  await Promise.all([done, ticks]);
  return trace;
}
let orders = 0;
for (const report of reports.filter(row => asyncKind(row.callable))) {
  assert.deepEqual(await completionOrder(report.source, report.callable),
    await completionOrder(reference(report.callable, report.case), report.callable),
    `${report.callable}/${report.case} completion job order`);
  ++orders;
}
console.log(`${cases} native callable tail scenarios, ${orders} completion job orders and queued async-generator completion passed.`);
