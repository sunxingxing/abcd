import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// File decoding and common family emission finish before this independent
// reference is evaluated. Partial output is never a behavioral oracle.
const reports = [];
for (const [flag, call, array, dead] of [
  ['--nested-literal-json', false, false, false], ['--nested-call-json', true, false, false],
  ['--nested-dead-read-json', false, false, true], ['--nested-dead-array-json', false, true, true]
]) {
  const rows = execFileSync(process.argv[2], [flag], {encoding: 'utf8'})
    .trim().split('\n').map(JSON.parse);
  assert.equal(rows.length, 3); reports.push(...rows.map(row => ({...row, call, array, dead})));
}
let scenarios = 0;
for (const {source, positions, call, array, dead} of reports) {
  assert.ok(!source.includes('INCOMPLETE'));
  assert.match(source, /return \{\n\s+run: \{run:/);
  assert.doesNotMatch(source, /run: \(\{/);
  const pair = vm.runInNewContext(`(() => {
    Object.defineProperty(Object.prototype, 'run', {set() { throw new Error('inherited setter'); }});
    return {
      recovered: (class { ${source} }).prototype.run,
      reference: (class { run(input) {
        try { return {run: {run: ${array ? '[]' : `input.run${call ? '()' : ''}`}}}; }
        catch (error) { return error; }
      } }).prototype.run,
      prototype: Object.prototype, arrayPrototype: Array.prototype
    };
  })()`);
  assert.equal(pair.recovered.name, 'run'); assert.equal(pair.recovered.length, 1);
  function exercise(fn, value, raised, proxy) {
    const trace = [], results = [];
    const callback = function() {
      assert.equal(this, input); assert.equal(arguments.length, 0); trace.push('call');
      if (raised === 'call') throw value;
      return value;
    };
    for (const key of ['call', 'apply', 'bind']) Object.defineProperty(callback, key, {
      get() { assert.fail('ambient invocation adapter'); }
    });
    const callable = proxy ? new Proxy(callback, {apply(fn, receiver, args) {
      assert.equal(receiver, input); trace.push('apply'); return Reflect.apply(fn, receiver, args);
    }}) : callback;
    const target = {get run() {
      assert.equal(this, input); trace.push('getter');
      target.effects = (target.effects ?? 0) + 1;
      if (raised === 'getter') throw value;
      return call ? callable : value;
    }};
    const input = proxy ? new Proxy(target, {get(object, key, receiver) {
      assert.equal(key, 'run'); trace.push('proxy');
      return Reflect.get(object, key, receiver);
    }}) : target;
    for (let i = 0; i < 2; ++i) {
      const result = Reflect.apply(fn, {wrongReceiver: true}, [input]);
      if (raised !== 'none' && !array) assert.equal(result, value);
      else {
        for (const object of [result, result.run]) {
          assert.equal(Object.getPrototypeOf(object), pair.prototype);
          assert.deepEqual(Reflect.ownKeys(object), ['run']);
          const descriptor = Object.getOwnPropertyDescriptor(object, 'run');
          assert.equal(descriptor.writable, true); assert.equal(descriptor.enumerable, true);
          assert.equal(descriptor.configurable, true);
        }
        if (array) {
          assert.ok(Array.isArray(result.run.run));
          assert.equal(Object.getPrototypeOf(result.run.run), pair.arrayPrototype);
          assert.deepEqual(Reflect.ownKeys(result.run.run), ['length']);
          assert.equal(result.run.run.length, 0);
          assert.deepEqual(Object.getOwnPropertyDescriptor(result.run.run, 'length'),
            {value: 0, writable: true, enumerable: false, configurable: false});
        } else assert.equal(result.run.run, value);
        assert.notEqual(result, result.run); results.push(result);
      }
      trace.push('return');
    }
    assert.equal(target.effects ?? 0, array ? 0 : 2, 'dead copies cannot add or remove getter effects');
    if (raised === 'none' || array) {
      assert.notEqual(results[0], results[1]); assert.notEqual(results[0].run, results[1].run);
      if (array) {
        assert.notEqual(results[0].run.run, results[1].run.run);
        results[0].run.run.push('changed'); assert.equal(results[1].run.run.length, 0);
      } else { results[0].run.run = 'changed'; assert.equal(results[1].run.run, value); }
    }
    return trace;
  }
  for (const value of [undefined, null, false, -0, NaN, 1n, Symbol(), {}, () => {}])
    for (const raised of (call ? ['none', 'getter', 'call'] : ['none', 'getter'])) for (const proxy of [false, true]) {
      assert.deepEqual(exercise(pair.recovered, value, raised, proxy),
        exercise(pair.reference, value, raised, proxy));
      ++scenarios;
    }
  for (const input of [undefined, null, false, 0, 1n, Symbol()]) {
    const actual = pair.recovered(input), expected = pair.reference(input);
    if (!array && (input == null || call)) assert.equal(actual.name, expected.name);
    else if (array) {
      assert.equal(actual.run.run.length, expected.run.run.length);
      assert.notEqual(actual.run.run, expected.run.run);
    } else assert.equal(actual.run.run, expected.run.run);
    ++scenarios;
  }
  // Exact original allocation, read, store and return sites survive the
  // shared printer; nested properties have their own definition origins.
  const pcs = array ? [0, 1, 3, 4, 6, 8, 10, 13, 18, 20, 25, 27, 28] :
    dead ? [0, 1, 3, 4, 6, 9, 12, 14, 18, 23, 25, 30, 32, 33] :
    call ? [0, 1, 3, 4, 6, 8, 12, 15, 20, 22, 27, 29, 30] :
    [0, 1, 3, 4, 6, 8, 12, 17, 19, 24, 26, 27];
  assert.equal(positions.operations.length, pcs.length); assert.equal(positions.properties.length, 2);
  const length = Buffer.byteLength(source);
  for (const placement of positions.operations) {
    const table = placement.kind === 'anchor' ? positions.anchors :
      placement.kind === 'property' ? positions.properties :
      placement.kind === 'statement' ? positions.statements : positions.expressions;
    const span = table[placement.node];
    assert.equal(positions.origins[span.origin].pc, pcs[placement.operation]);
    if (placement.kind === 'anchor') assert.ok(span.byte <= length);
    else assert.ok(span.begin < span.end && span.end <= length);
    if (dead && (placement.operation === 5 || (array && placement.operation === 6) ||
        (!array && placement.operation === 4))) assert.equal(placement.kind, 'anchor');
  }
}
console.log(`Phased nested literals: ${scenarios} getter/Proxy/identity/exception scenarios passed.`);
