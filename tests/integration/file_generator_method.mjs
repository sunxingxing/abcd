import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const fixtures = execFileSync(process.argv[2], ['--generator-method-json'],
  {encoding: 'utf8', timeout: 60_000}).trim().split('\n').map(line => JSON.parse(line));
assert.equal(fixtures.length, 36);
for (const fixture of fixtures) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  // The enclosure is supplied by the test; all member syntax and body text
  // are emitted from actual ABC decoding through the common family printer.
  const {Recovered} = await import(`data:text/javascript;base64,${Buffer.from(`export class Recovered { ${fixture.source} }`).toString('base64')}`);
  const owner = fixture.static ? Recovered : new Recovered();
  const holder = fixture.static ? Recovered : Recovered.prototype;
  const method = owner.run, descriptor = Object.getOwnPropertyDescriptor(holder, 'run');
  assert.equal(method.name, 'run'); assert.equal(method.length, 1);
  assert.equal(Object.getPrototypeOf(method), Object.getPrototypeOf(function* () {}));
  assert.equal(Object.hasOwn(method, 'prototype'), true);
  assert.throws(() => Reflect.construct(method, []), TypeError);
  assert.equal(descriptor.enumerable, false);
  assert.equal(descriptor.configurable, true); assert.equal(descriptor.writable, true);
  const argument = payload => fixture.catch ? function () {
    assert.equal(this, undefined); return payload;
  } : payload;
  const direct = owner.run(argument('yielded'));
  assert.deepEqual(direct.next(), {value: 'yielded', done: false});
  assert.deepEqual(direct.next(), {value: owner, done: true});
  for (const receiver of [undefined, null, false, 0, NaN, 'text', Symbol('receiver'), {}]) {
    for (const payload of [undefined, null, Symbol('yield'), {}, {get then() { assert.fail('sync yield assimilated thenable'); }}]) {
      const iterator = Reflect.apply(method, receiver, [argument(payload)]);
      assert.equal(Object.getPrototypeOf(iterator), method.prototype);
      assert.equal(iterator[Symbol.iterator](), iterator);
      const first = iterator.next('ignored');
      assert.equal(first.done, false); assert.ok(Object.is(first.value, payload));
      const last = iterator.next('resumed');
      assert.equal(last.done, true); assert.ok(Object.is(last.value, receiver));
      assert.deepEqual(iterator.next(), {value: undefined, done: true});
      for (const initial of [false, true]) {
        for (const completion of ['return', 'throw']) {
          const other = Reflect.apply(method, receiver, [argument(payload)]);
          if (!initial) other.next();
          if (completion === 'return' || (!initial && fixture.catch)) {
            const result = other[completion](payload);
            assert.equal(result.done, true); assert.ok(Object.is(result.value, payload));
          } else {
            let thrown = false;
            try { other.throw(payload); } catch (error) { thrown = true; assert.ok(Object.is(error, payload)); }
            assert.equal(thrown, true);
          }
          assert.deepEqual(other.next(), {value: undefined, done: true});
        }
      }
    }
    const bound = method.bind(receiver), iterator = bound(argument(undefined));
    iterator.next(); assert.ok(Object.is(iterator.next().value, receiver));
  }
  const aThis = {}, bThis = {}, a = method.call(aThis, argument('a')), b = method.call(bThis, argument('b'));
  assert.deepEqual(a.next(), {value: 'a', done: false});
  assert.deepEqual(b.next(), {value: 'b', done: false});
  assert.equal(b.next().value, bThis); assert.equal(a.next().value, aThis);
  if (fixture.catch) {
    for (const error of [undefined, null, false, 0, NaN, Symbol('callback'), {}]) {
      let calls = 0;
      const iterator = owner.run(function () { assert.equal(this, undefined); ++calls; throw error; });
      assert.equal(calls, 0);
      const result = iterator.next();
      assert.equal(result.done, true); assert.ok(Object.is(result.value, error)); assert.equal(calls, 1);
      assert.deepEqual(iterator.next(), {value: undefined, done: true});
    }
    const iterator = owner.run(() => assert.fail('early return evaluated callback'));
    assert.deepEqual(iterator.return('early'), {value: 'early', done: true});
  }
}
