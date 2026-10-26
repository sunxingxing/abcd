import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
const [tool, abc, paired] = process.argv.slice(2);
// ABC-only generation finishes before the independent reference is opened.
const run = spawnSync(tool, [abc, '8431', '--record-source', '--value-source', '--language-source'],
  {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr);
const report = JSON.parse(run.stdout);
assert.equal(report.record, 7618);
assert.equal(report.source_complete, true);
assert.equal(report.legacy_writer_used, undefined);
assert.doesNotMatch(report.source, /INCOMPLETE|UNRECOVERED/);
const referenceText = readFileSync(paired, 'utf8');
assert.equal((referenceText.match(/^@Observed$/gm) ?? []).length, 1);
const reference = stripTypeScriptTypes(referenceText.replace(/^@Observed$/gm, ''), {mode: 'transform'});
const token = {original: 'exception'};
async function load(source) {
  let trace, failAt, steps, argumentIds;
  const event = (...parts) => {
    trace.push(parts);
    if (++steps === failAt) throw token;
  };
  const math = new Proxy(Math, {get(target, key, receiver) {
    assert.equal(receiver, math);
    event('Math.get', key);
    const value = Reflect.get(target, key);
    if (typeof value !== 'function') return value;
    return function(...args) {
      assert.equal(this, math);
      event('Math.call', key, ...args.map(value => {
        if (value === null || typeof value !== 'object') return value;
        assert.ok(argumentIds.has(value)); return {argument: argumentIds.get(value)};
      }));
      return Reflect.apply(value, Math, args);
    };
  }});
  const context = vm.createContext({Math: math, Number: function(value) {
    assert.equal(this, undefined); event('Number.call', value); return Number(value);
  }, Observed: constructor => constructor});
  const module = new vm.SourceTextModule(source, {context});
  await module.link(() => new vm.SyntheticModule(['default'], function() {
    this.setExport('default', {});
  }, {context}));
  await module.evaluate();
  const fn = module.namespace.WindowUtil.prototype.getCalDegree;
  return {text: fn.toString(), observe(values, coercion = false, failure = 0) {
    trace = []; failAt = failure; steps = 0; argumentIds = new WeakMap();
    const args = coercion ? values.map((value, index) => {
      let reads = 0;
      const argument = {get [Symbol.toPrimitive]() {
        event('coercion.get', index);
        return function(hint) {
          assert.equal(this, argument); event('coercion.call', index, hint);
          // Repeated coercion may change. No prior multiply grants a type
          // or value fact to the original parameter's later unary minus.
          return reads++ % 2 ? value.next : value.first;
        };
      }};
      argumentIds.set(argument, index); return argument;
    }) : values;
    try { return {value: fn.apply({}, args), trace}; }
    catch (error) {
      if (error === token) return {originalException: true, trace};
      assert.ok(['TypeError', 'RangeError'].includes(error.name), String(error));
      return {error: error.name, trace};
    }
  }};
}
const actual = await load(report.source), expected = await load(reference);
assert.match(actual.text, /-x/);
assert.match(actual.text, /Math\.atan2/);
assert.match(actual.text, /% 360/);
assert.doesNotMatch(actual.text, /Reflect\.|Function\(|switch\s*\(/);
const view = new DataView(new ArrayBuffer(8));
view.setBigUint64(0, 0x7fff000000000005n); const payloadNaN = view.getFloat64(0);
const values = [undefined, null, false, true, 0, -0, 1, -1, 2, -2,
  0.5, -2147483648, 2147483648, Number.MIN_VALUE, Number.MAX_VALUE,
  Infinity, -Infinity, NaN, payloadNaN, '', '2', 'text', 0n, 2n, Symbol('input')];
let scenarios = 0;
function compare(args, coercion = false, failure = 0) {
  assert.deepEqual(actual.observe(args, coercion, failure), expected.observe(args, coercion, failure));
  ++scenarios;
}
for (const x of values) for (const y of values) for (const z of values) compare([x, y, z]);
for (const first of [0, -0, 1, -1, 10, NaN, Infinity, '2', 'text', 2n, Symbol('coercion')])
  for (const next of [1, -1, NaN, '2', 2n]) for (let failure = 0; failure <= 40; ++failure)
    compare([{first, next}, {first: 2, next}, {first: 1, next: 4}], true, failure);
// Independent mutations must be observable, rather than vacuous acceptance.
for (const [from, to] of [['-x', 'x'], ['% 360', '% 180'], ['Math.atan2(y, -x)', 'Math.atan2(-x, y)']]) {
  assert.ok(report.source.includes(from), from);
  const mutated = await load(report.source.replace(from, to));
  let detected = false;
  for (const args of [[1, 2, 0], [-1, -2, 0], [2, -1, 0]]) {
    try {assert.deepEqual(mutated.observe(args), expected.observe(args));}
    catch (error) {if (!(error instanceof assert.AssertionError)) throw error; detected = true; break;}
  }
  assert.equal(detected, true, from);
}
console.log(JSON.stringify({recordSourceComplete: true, pairedMethodScenarios: scenarios,
  mutationChecks: 3, wholeRecordBehavior: false, platformExecution: false}));
