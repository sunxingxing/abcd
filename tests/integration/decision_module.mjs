import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const [tool, mode] = process.argv.slice(2);
const phased = mode === "--phased";
const failure = {}, token = {};
const hostile = {[Symbol.toPrimitive]() { assert.fail('truthiness must not coerce'); }};
const values = [undefined, null, false, -0, NaN, 1, 2n, Symbol('value'), token, hostile];
let scenarios = 0;
for (const conjunction of [false, true]) for (const direct of [false, true]) for (const count of [1, 2]) {
  const option = `--${phased ? "phased-" : ""}decision-${conjunction ? 'and' : 'or'}-${direct ? 'direct-' : ''}source`;
  const source = execFileSync(tool, [option, ...(count === 2 ? ['--twice'] : [])], {encoding: 'utf8'});
  assert.ok(source.includes('if (') && !source.includes('INCOMPLETE'));
  const condition = `value.probe ${conjunction ? '&&' : '||'} imported${direct ? '' : '.option'}`;
  const oracle = `(function(value) { ${phased ? "try {" : ""} for (let i = 0; i < ${count}; ++i) {
    if (${condition}) value.method(); else value.other();
  } ${phased ? "} catch (error) { return error; }" : ""} })`;
  const pair = vm.runInNewContext(`(() => { let imported; return {
    generated: ${source}, oracle: ${oracle}, set(value) { imported = value; }
  }; })()`);
  function run(fn, probe, optionValue, failAt, missing = false) {
    const events = []; let step = 0;
    const event = label => { events.push(label); if (++step === failAt) throw failure; };
    const namespace = {get option() { event('option'); return optionValue; }};
    const value = {
      get probe() {
        event('probe'); pair.set(missing ? undefined : direct ? optionValue : namespace); return probe;
      },
      get method() {
        event('method'); return function() { assert.equal(this, value); event('yes'); return token; };
      },
      get other() {
        event('other'); return function() { assert.equal(this, value); event('no'); return token; };
      }
    };
    let returned = false;
    try { const result = fn(value); returned = true; if (phased && result !== undefined) throw result; assert.equal(result, undefined); events.push('return'); }
    catch (error) {
      if (phased) assert.ok(returned, 'the bytecode catch must return the thrown value');
      if (error === failure) events.push('sentinel');
      else if (missing && !direct && error.name === 'TypeError') events.push('TypeError');
      else throw error;
    }
    return events;
  }
  for (const probe of values) for (const optionValue of values) for (let failAt = 0; failAt <= 8; ++failAt) {
    assert.deepEqual(run(pair.generated, probe, optionValue, failAt), run(pair.oracle, probe, optionValue, failAt));
    ++scenarios;
  }
  for (const probe of [false, true]) {
    assert.deepEqual(run(pair.generated, probe, undefined, 0, true), run(pair.oracle, probe, undefined, 0, true));
    ++scenarios;
  }
  // Invoke before initialization: a short-circuited RHS must not read the
  // lexical binding; the evaluated RHS must throw before choosing a call arm.
  const tdz = vm.runInNewContext(`(() => {
    const functions = [${source}, ${oracle}], results = [];
    for (const fn of functions) for (const probe of [false, true]) {
      const events = []; let returned = false;
      try { const result = fn({probe, method() { events.push('yes'); }, other() { events.push('no'); }}); returned = true; if (result !== undefined) throw result; }
      catch (error) { if (${phased} && !returned) throw error; events.push(error.name); }
      results.push(events);
    }
    let imported; return results;
  })()`);
  assert.deepEqual(Array.from(tdz[0]), Array.from(tdz[2]));
  assert.deepEqual(Array.from(tdz[1]), Array.from(tdz[3]));
  assert.deepEqual(Array.from(tdz[conjunction ? 1 : 0]), ['ReferenceError']);
  assert.deepEqual(Array.from(tdz[conjunction ? 0 : 1]), Array(count).fill(conjunction ? 'no' : 'yes'));
  scenarios += 4;
}
console.log(`Module guards in decisions: ${scenarios} independent short-circuit/effect/TDZ scenarios passed`);
