import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// References stay outside ABC decoding. Only complete common-family functions
// reach this oracle; getter-before-arguments is part of native call semantics.
let scenarios = 0;
for (const shape of ['nested', 'array', 'pair']) {
  const flag = shape === 'nested' ? '--literal-argument-json' :
    shape === 'array' ? '--literal-array-argument-json' : '--literal-arguments-json';
  const reports = execFileSync(process.argv[2], [flag], {encoding: 'utf8'})
    .trim().split('\n').map(JSON.parse);
  assert.equal(reports.length, 3);
  for (const {source, positions} of reports) {
    assert.ok(!source.includes('INCOMPLETE'));
    const expression = shape === 'nested' ? 'input.run({run: {run: input.run}})' :
      shape === 'array' ? 'input.run({run: {run: []}})' : 'input.run({run: input.run}, {run: input.run})';
    const pair = vm.runInNewContext(`(() => {
      Object.defineProperty(Object.prototype, 'run', {set() { throw new Error('inherited setter'); }});
      return {
        recovered: (class { ${source} }).prototype.run,
        reference: (class { run(input) { try { return ${expression}; } catch (error) { return error; } } }).prototype.run,
        prototype: Object.prototype, arrayPrototype: Array.prototype
      };
    })()`);
    assert.equal(pair.recovered.length, 1); assert.equal(pair.recovered.name, 'run');
    function exercise(fn, value, raised, calleeKind, proxy) {
      const trace = [], calls = [];
      let reads = 0;
      const callback = function(...args) {
        assert.equal(this, input); trace.push('call');
        assert.equal(args.length, shape === 'pair' ? 2 : 1);
        for (const object of [...args, ...(shape !== 'pair' ? [args[0].run] : [])]) {
          assert.equal(Object.getPrototypeOf(object), pair.prototype);
          assert.deepEqual(Reflect.ownKeys(object), ['run']);
          const descriptor = Object.getOwnPropertyDescriptor(object, 'run');
          assert.equal(descriptor.writable, true); assert.equal(descriptor.enumerable, true);
          assert.equal(descriptor.configurable, true);
        }
        if (shape === 'pair') {
          assert.notEqual(args[0], args[1]);
          assert.equal(args[0].run, value); assert.equal(args[1].run, value);
        } else if (shape === 'array') {
          const array = args[0].run.run;
          assert.ok(Array.isArray(array)); assert.equal(Object.getPrototypeOf(array), pair.arrayPrototype);
          assert.deepEqual(Reflect.ownKeys(array), ['length']); assert.equal(array.length, 0);
        } else assert.equal(args[0].run.run, value);
        calls.push(args);
        if (raised === 'call') throw value;
        return value;
      };
      for (const key of ['call', 'apply', 'bind']) Object.defineProperty(callback, key, {
        get() { assert.fail('ambient invocation adapter'); }
      });
      const callable = calleeKind === 'proxy' ? new Proxy(callback, {apply(target, receiver, args) {
        assert.equal(receiver, input); trace.push('apply'); return Reflect.apply(target, receiver, args);
      }}) : calleeKind === 'object' ? {} : calleeKind === 'class' ? class {} : callback;
      const target = {get run() {
        ++reads; trace.push(`getter:${reads}`); target.effects = (target.effects ?? 0) + 1;
        if (raised === `getter:${reads}`) throw value;
        // The argument read can change the member while the originally read
        // callable must remain the invocation target.
        if (reads !== 1) target.changed = true;
        return reads === 1 ? callable : value;
      }};
      const input = proxy ? new Proxy(target, {get(object, key, receiver) {
        assert.equal(key, 'run'); trace.push('proxy'); return Reflect.get(object, key, receiver);
      }}) : target;
      for (let i = 0; i < 2; ++i) {
        reads = 0;
        const result = Reflect.apply(fn, {wrongReceiver: true}, [input]);
        const getterThrow = raised.startsWith('getter:');
        if (getterThrow || (raised === 'call' && (calleeKind === 'callable' || calleeKind === 'proxy')))
          assert.equal(result, value);
        else if (calleeKind === 'object' || calleeKind === 'class') assert.equal(result.name, 'TypeError');
        else assert.equal(result, value);
        trace.push('return');
      }
      if (calls.length === 2) {
        assert.notEqual(calls[0][0], calls[1][0]);
        if (shape === 'pair') assert.notEqual(calls[0][1], calls[1][1]);
        else {
          assert.notEqual(calls[0][0].run, calls[1][0].run);
          if (shape === 'array') {
            assert.notEqual(calls[0][0].run.run, calls[1][0].run.run);
            calls[0][0].run.run.push('changed'); assert.equal(calls[1][0].run.run.length, 0);
          }
        }
      }
      assert.equal(target.effects, trace.filter(event => event.startsWith('getter:')).length);
      return trace;
    }
    const raised = ['none', 'getter:1', ...(shape !== 'array' ? ['getter:2'] : []),
      ...(shape === 'pair' ? ['getter:3'] : []), 'call'];
    for (const value of [undefined, null, false, -0, NaN, 1n, Symbol(), {}, () => {}])
      for (const event of raised) for (const callee of ['callable', 'proxy', 'object', 'class'])
        for (const proxy of [false, true]) {
          assert.deepEqual(exercise(pair.recovered, value, event, callee, proxy),
            exercise(pair.reference, value, event, callee, proxy));
          ++scenarios;
        }
    for (const input of [undefined, null, false, 0, 1n, Symbol()]) {
      assert.equal(pair.recovered(input).name, pair.reference(input).name); ++scenarios;
    }
    const pcs = shape === 'nested' ? [0, 2, 6, 8, 9, 11, 12, 14, 16, 20, 25, 27, 32, 34, 38, 39] :
      shape === 'array' ? [0, 2, 6, 8, 9, 11, 12, 14, 16, 21, 23, 28, 30, 34, 35] :
      [0, 2, 6, 8, 9, 11, 13, 17, 22, 23, 25, 27, 31, 36, 38, 43, 44];
    assert.equal(positions.operations.length, pcs.length);
    const length = Buffer.byteLength(source);
    for (const placement of positions.operations) {
      const table = placement.kind === 'anchor' ? positions.anchors : placement.kind === 'property' ? positions.properties :
        placement.kind === 'statement' ? positions.statements : positions.expressions;
      const span = table[placement.node]; assert.equal(positions.origins[span.origin].pc, pcs[placement.operation]);
      if (placement.kind === 'anchor') assert.ok(span.byte <= length);
      else assert.ok(span.begin < span.end && span.end <= length);
    }
  }
}
console.log(`Phased literal arguments: ${scenarios} native reference/order/Proxy/identity/throw scenarios passed.`);
