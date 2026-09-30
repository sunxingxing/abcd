import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

// The generated callable, not a wrapper or invoking iterator, is LDFUNCTION's
// result. Strict dynamic this must also survive independent suspended frames.
for (const mode of ['self', 'self-private', 'this', 'receiver']) {
  const source = execFileSync(process.argv[2], [`--yield-${mode}`], {encoding: 'utf8', timeout: 60_000});
  assert.ok(!source.includes('INCOMPLETE'));
  const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
  assert.equal(functions.length, 9);
  const self = mode.startsWith('self');
  for (const [index, capture] of functions.entries()) {
    const saved = [], cells = [{}, {}]; let iteration = 0;
    capture(() => iteration < cells.length, () => {
      const cell = cells[iteration++];
      if (index % 3 === 1) throw cell;
      return cell;
    }, get => saved.push(get));
    assert.equal(saved.length, 2);
    assert.notEqual(saved[0], saved[1]);
    assert.notEqual(saved[0].prototype, saved[1].prototype);
    for (const get of saved) {
      assert.equal(get.name, mode === 'self-private' ? 'get value' : 'get');
      assert.equal(get.length, 1);
      assert.equal(Object.getPrototypeOf(get), Object.getPrototypeOf(function* () {}));
      assert.equal(Object.hasOwn(get, 'prototype'), true);
      assert.throws(() => Reflect.construct(get, []), TypeError);
      for (const receiver of [undefined, null, false, 0, NaN, 'text', Symbol('this'), {}]) {
        const payload = {get then() { assert.fail('sync generator must not assimilate thenable'); }};
        const iterator = Reflect.apply(get, receiver, [payload]);
        assert.equal(Object.getPrototypeOf(iterator), get.prototype);
        assert.deepEqual(iterator.next('ignored'), {value: payload, done: false});
        const result = iterator.next('resumed');
        assert.equal(result.done, true); assert.ok(Object.is(result.value, self ? get : receiver));
        assert.deepEqual(iterator.next(), {value: undefined, done: true});

        const bound = get.bind(receiver), second = bound(payload);
        second.next();
        assert.ok(Object.is(second.next().value, self ? get : receiver));
        for (const completion of ['return', 'throw']) {
          const early = Reflect.apply(get, receiver, [payload]), error = {};
          early.next();
          if (completion === 'return') assert.deepEqual(early.return(error), {value: error, done: true});
          else assert.throws(() => early.throw(error), actual => actual === error);
        }
      }
      const first = {}, second = {}, a = get.call(first, 'a'), b = get.call(second, 'b');
      assert.deepEqual(a.next(), {value: 'a', done: false});
      assert.deepEqual(b.next(), {value: 'b', done: false});
      assert.equal(b.next().value, self ? get : second);
      assert.equal(a.next().value, self ? get : first);
    }
  }
}
