import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
const reports = execFileSync(process.argv[2], [], {encoding: 'utf8', maxBuffer: 16 * 1024 * 1024})
  .trim().split('\n').map(line => JSON.parse(line));
assert.equal(reports.length, 276);
const values = [undefined, null, false, true, 0, -0, NaN, Infinity, -Infinity, '', 'text', 0n, 1n, Symbol('input'), 'object', 'function'];
const marker = {}, failure = Symbol('failure');
function observe(fn, inputKind, mutate, failAt, form) {
  const events = [];
  const event = label => { events.push(label); if (events.length === failAt) throw failure; };
  const payload = inputKind === 'object' ? {} : inputKind === 'function' ? function(...args) {
    assert.equal(this, undefined); event('value-call');
    assert.equal(args.length, form === 17 ? 1 : 0);
    if (args.length) assert.equal(args[0], marker);
    return marker;
  } : inputKind;
  if (typeof payload === 'function' || (payload !== null && typeof payload === 'object'))
    Object.defineProperty(payload, 'field', {get() {
      assert.equal(this, payload); event('field');
      if (form >= 18) {
        const next = {get field() {
          assert.equal(this, next); event('next-field');
          if (form >= 20) return function() {assert.equal(this, next); assert.equal(arguments.length, 0); event('chain-call'); return marker;};
          return marker;
        }};
        return next;
      }
      if (form >= 8 && form !== 10 && form !== 16 && form !== 17)
        return function(...args) {
          assert.equal(this, form === 13 ? undefined : payload); event('member-call');
          assert.equal(args.length, form === 12 || form === 13 ? 1 : 0);
          if (args.length) assert.equal(args[0], marker);
          return marker;
        };
      return marker;
    }});
  let input;
  input = {get probe() {
    assert.equal(this, input); event('probe');
    if (mutate) Object.defineProperty(input, 'probe', {get() { assert.fail('replayed producer'); }});
    return form === 6 ? function() { assert.equal(this, input); event('call'); return payload; } : payload;
  }, get field() {
    assert.equal(this, input); event('different-field');
    return form === 11 ? function() {assert.equal(this, input); event('different-call'); return marker;} : marker;
  }, get argument() {assert.equal(this, input); event('argument'); return marker;},
  get key() {
    assert.equal(this, input); event('key');
    return {[Symbol.toPrimitive](hint) {assert.equal(hint, 'string'); event('key-conversion'); return 'field';}};
  }};
  try { return {events, value: fn(input), threw: false}; }
  catch (error) {
    if (error === failure) return {events, value: error, threw: true};
    if (error?.name === 'TypeError') return {events, value: 'TypeError', threw: true};
    throw error;
  }
}
let scenarios = 0;
for (const report of reports) {
  assert.equal(report.source.includes('?.'), ![3, 4, 11].includes(report.form));
  const generated = vm.runInNewContext(report.source);
  const oracle = value => {
    const kept = report.form === 6 ? value.probe() : value.probe;
    if (report.form === 3) return kept === null ? undefined : kept.field;
    if (report.form === 4) return kept === null || kept === undefined ? undefined : value.field;
    if ([8, 14, 15].includes(report.form)) return kept?.field();
    if (report.form === 9) return kept?.[value.key]();
    if (report.form === 10 || report.form === 16) return kept?.();
    if (report.form === 11) return kept === null || kept === undefined ? undefined : value.field();
    if (report.form === 12) return kept?.field(value.argument);
    if (report.form === 13) return (0, kept?.field)(value.argument);
    if (report.form === 17) return kept?.(value.argument);
    if (report.form === 18) return kept?.field.field;
    if (report.form === 19) return kept?.field[value.key];
    if (report.form === 20 || report.form === 22) return kept?.field.field();
    if (report.form === 21) return kept?.field[value.key]();
    return kept?.field;
  };
  for (const input of values) for (const mutate of [false, true]) for (const failAt of [0, 1, 2, 3, 4, 5, 6]) {
    const actual = observe(generated, input, mutate, failAt, report.form);
    const expected = observe(oracle, input, mutate, failAt, report.form);
    assert.deepEqual(actual.events, expected.events);
    assert.equal(actual.threw, expected.threw);
    assert.ok(Object.is(actual.value, expected.value), `profile=${report.profile} count=${report.count} form=${report.form}`);
    ++scenarios;
  }
}
assert.equal(scenarios, 61824);
// Optional value calls retain indirect eval even when their saved callable is
// the intrinsic eval. A direct eval would expose the generated local binding.
for (const report of reports.filter(report => report.form === 17)) {
  const generated = vm.runInNewContext(report.source);
  assert.equal(generated({probe: eval, argument: 'typeof $value1'}), 'undefined');
}
console.log(JSON.stringify({generatedFixtures: reports.length, scenarios}));
