import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// Execute only complete common-family output; references are outside engine
// inputs. Constant NEG is deliberately certified separately from general ~.
let scenarios = 0;
for (const constant of [true, false]) {
  const reports = execFileSync(process.argv[2],
    [constant ? '--nested-negate-json' : '--nested-bitnot-json'], {encoding: 'utf8'})
    .trim().split('\n').map(JSON.parse);
  assert.equal(reports.length, 3);
  for (const {source, positions} of reports) {
    assert.ok(!source.includes('INCOMPLETE'));
    const pair = vm.runInNewContext(`(() => {
      Object.defineProperty(Object.prototype, 'run', {set() { throw new Error('inherited setter'); }});
      return {
        recovered: (class { ${source} }).prototype.run,
        reference: (class { run(input) {
          try { return {run: {run: ${constant ? '-1' : '~input.run'}}}; }
          catch (error) { return error; }
        } }).prototype.run,
        prototype: Object.prototype
      };
    })()`);
    assert.equal(pair.recovered.name, 'run'); assert.equal(pair.recovered.length, 1);
    function exercise(fn, value, mode, proxy) {
      const trace = [], outputs = [];
      const sentinel = {thrown: true};
      const numeric = mode.startsWith('coerce') ? {
        [Symbol.toPrimitive](hint) {
          trace.push(`primitive:${hint}`);
          if (mode === 'coerce-throw') throw sentinel;
          if (mode === 'coerce-invalid') return {};
          return value;
        }
      } : mode === 'fallback' ? {
        valueOf() { trace.push('valueOf'); return {}; },
        toString() { trace.push('toString'); return '5'; }
      } : value;
      const target = {get run() {
        trace.push('getter'); target.effects = (target.effects ?? 0) + 1;
        if (mode === 'getter-throw') throw sentinel;
        return numeric;
      }};
      const input = proxy ? new Proxy(target, {get(object, key, receiver) {
        assert.equal(key, 'run'); trace.push('proxy');
        return Reflect.get(object, key, receiver);
      }}) : target;
      for (let i = 0; i < 2; ++i) {
        const result = Reflect.apply(fn, {wrongReceiver: true}, [input]);
        if (!constant && (mode === 'getter-throw' || mode === 'coerce-throw')) {
          assert.equal(result, sentinel); outputs.push('sentinel');
        } else if (!constant && (mode === 'coerce-invalid' || typeof value === 'symbol')) {
          assert.equal(result.name, 'TypeError'); outputs.push(result.name);
        } else {
          for (const object of [result, result.run]) {
            assert.equal(Object.getPrototypeOf(object), pair.prototype);
            assert.deepEqual(Reflect.ownKeys(object), ['run']);
            const descriptor = Object.getOwnPropertyDescriptor(object, 'run');
            assert.equal(descriptor.writable, true); assert.equal(descriptor.enumerable, true);
            assert.equal(descriptor.configurable, true);
          }
          outputs.push(result.run.run);
          if (i === 0) exercise.first = result;
          else {
            assert.notEqual(result, exercise.first);
            assert.notEqual(result.run, exercise.first.run);
            exercise.first.run.run = 'changed';
            assert.notEqual(result.run.run, 'changed');
          }
        }
        trace.push('return');
      }
      assert.equal(target.effects ?? 0, constant ? 0 : 2);
      return {trace, outputs};
    }
    for (const value of [undefined, null, false, true, 0, -0, NaN, Infinity, -Infinity,
      1, -7, 4294967297, '', '12', 'bad', 1n, -9n, Symbol()]) {
      for (const mode of ['plain', 'getter-throw', 'coerce', 'coerce-throw', 'coerce-invalid']) {
        for (const proxy of [false, true]) {
          assert.deepEqual(exercise(pair.recovered, value, mode, proxy),
            exercise(pair.reference, value, mode, proxy));
          ++scenarios;
        }
      }
    }
    for (const proxy of [false, true]) {
      assert.deepEqual(exercise(pair.recovered, 5, 'fallback', proxy),
        exercise(pair.reference, 5, 'fallback', proxy));
      ++scenarios;
    }
    for (const input of [undefined, null, false, 0, 1n, Symbol()]) {
      const actual = pair.recovered(input), expected = pair.reference(input);
      if (!constant && input == null) assert.equal(actual.name, expected.name);
      else assert.equal(actual.run.run, expected.run.run);
      ++scenarios;
    }
    // Original unary/constant and property sites survive the common printer.
    const pcs = constant ? [0, 1, 3, 4, 6, 11, 13, 18, 20, 25, 27, 28] :
      [0, 1, 3, 4, 6, 8, 12, 14, 19, 21, 26, 28, 29];
    assert.equal(positions.operations.length, pcs.length);
    assert.equal(positions.properties.length, 2);
    const length = Buffer.byteLength(source);
    for (const placement of positions.operations) {
      const table = placement.kind === 'anchor' ? positions.anchors :
        placement.kind === 'property' ? positions.properties :
        placement.kind === 'statement' ? positions.statements : positions.expressions;
      const span = table[placement.node];
      const origin = positions.origins[span.origin];
      if (origin.kind === 'combined') {
        assert.ok(origin.part_count > 0);
        const parts = positions.origin_parts.slice(origin.first_part, origin.first_part + origin.part_count);
        assert.equal(parts.length, origin.part_count);
        for (const part of parts) assert.equal(positions.origins[part].pc, pcs[placement.operation]);
      } else assert.equal(origin.pc, pcs[placement.operation]);
      if (placement.kind === 'anchor') assert.ok(span.byte <= length);
      else assert.ok(span.begin < span.end && span.end <= length);
    }
  }
}
console.log(`Phased unary literals: ${scenarios} coercion/getter/Proxy/identity/exception scenarios passed.`);
