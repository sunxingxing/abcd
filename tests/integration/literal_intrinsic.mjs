import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

function reference(probe) {
  'use strict';
  return {
    field0: {receiver: this, self: reference, effect: probe(this, reference)},
    field1: {receiver: this, self: reference, effect: probe(this, reference)}
  };
}
let scenarios = 0;
for (const option of ['--source', '--copy-source']) {
  const source = execFileSync(process.argv[2], [option], {encoding: 'utf8'});
  assert.ok(!source.includes('INCOMPLETE'));
  assert.ok(source.includes('receiver: this') && source.includes('self: recovered'));
  const recovered = vm.runInNewContext(`'use strict'; ${source}`);
  assert.equal(recovered.length, 1);
  assert.equal(recovered.name, 'recovered');
  function run(fn, receiver, fail, proxy, construct) {
    const events = [], payloads = [], thrown = {};
    let calls = 0, observed;
    function probe(arg, self) {
      'use strict';
      assert.equal(this, undefined);
      assert.equal(self, fn);
      if (construct) {
        if (calls) assert.equal(arg, observed);
        else { observed = arg; assert.equal(Object.getPrototypeOf(arg), Derived.prototype); }
      } else assert.equal(arg, receiver);
      events.push(['call', calls]);
      if (++calls === fail) throw thrown;
      const value = {calls}; payloads.push(value); return value;
    }
    const callback = proxy ? new Proxy(probe, {apply(target, thisArg, args) {
      assert.equal(thisArg, undefined); assert.equal(args.length, 2);
      events.push(['apply', calls]); return Reflect.apply(target, thisArg, args);
    }}) : probe;
    function Derived() {}
    try {
      const result = construct ? Reflect.construct(fn, [callback], Derived) : Reflect.apply(fn, receiver, [callback]);
      assert.deepEqual(Object.keys(result), ['field0', 'field1']);
      assert.notEqual(result.field0, result.field1);
      for (const [i, key] of ['field0', 'field1'].entries()) {
        const object = result[key];
        assert.deepEqual(Object.keys(object), ['receiver', 'self', 'effect']);
        assert.equal(object.receiver, construct ? observed : receiver);
        assert.equal(object.self, fn); assert.equal(object.effect, payloads[i]);
        for (const field of Object.keys(object)) {
          const descriptor = Object.getOwnPropertyDescriptor(object, field);
          assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
        }
      }
      events.push(['return']);
    } catch (error) {
      if (error !== thrown) throw error;
      events.push(['throw', calls]);
    }
    return events;
  }
  for (const receiver of [undefined, null, false, 0, -0, 1n, '', Symbol('receiver'), {}, new Proxy({}, {})])
    for (const fail of [0, 1, 2]) for (const proxy of [false, true]) for (const construct of [false, true]) {
      assert.deepEqual(run(recovered, receiver, fail, proxy, construct), run(reference, receiver, fail, proxy, construct));
      ++scenarios;
    }
  for (const callback of [undefined, null, false, 0, {}, Symbol('noncallable')])
    assert.throws(() => Reflect.apply(recovered, {}, [callback]), error => error.name === 'TypeError');
}
// Unit AST inputs expose the VM predicates as expressions. Compare their
// actual lowering, including an earlier getter snapshot, independently.
const predicates = execFileSync(process.argv[3], ['--predicate-sources'], {encoding: 'utf8'});
const forms = vm.runInNewContext(`'use strict'; [${predicates.trim().replaceAll(/\)\s*\n\s*\(/g, '),(')}]`);
assert.equal(forms.length, 3);
for (const value of [undefined, null, false, true, 0, -0, 1, -1, NaN, Infinity, 0n, 1n, '', '0', {}, Symbol('value')]) {
  assert.equal(forms[0](value).value, value === false || value === 0);
  assert.equal(forms[1](value).value, (!value) === false || (!value) === 0);
  for (const fail of [false, true]) {
    const thrown = {}; let reads = 0;
    const input = {get probe() { ++reads; if (fail) throw thrown; return value; }};
    if (fail) assert.throws(() => forms[2](input), error => error === thrown);
    else assert.equal(forms[2](input).value, value === false || value === 0);
    assert.equal(reads, 1); ++scenarios;
  }
}
console.log(`Literal source capabilities: ${scenarios} identity, receiver, call, exception and predicate scenarios passed.`);
