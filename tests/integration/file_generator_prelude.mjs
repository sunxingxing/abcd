import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

// Losing the argument copy or attaching the generator's own environment to
// its parent corrupts payloads, nested reader lifetimes or sibling iterators.
for (const mode of ['copy', 'copy4', 'copy16', 'copy-many', 'lexical', 'closure', 'closure4', 'closure16',
  'frame-copy', 'frame-copy4', 'frame-copy16', 'parameter-cell',
  'parameter-frame-cell', 'parameter-frame-cell4', 'parameter-frame-cell16']) {
  const kind = mode.startsWith('frame-copy') ? 'frame-copy' : mode.startsWith('copy') ? 'copy' :
    mode.startsWith('closure') || mode.startsWith('parameter-') ? 'closure' : 'lexical';
  const source = execFileSync(process.argv[2], [`--yield-${mode}`], {encoding: 'utf8', timeout: 60_000});
  assert.ok(!source.includes('INCOMPLETE'));
  const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
  assert.equal(functions.length, mode === 'copy-many' ? 3 : 9);
  for (const [index, capture] of functions.entries()) {
    const saved = [], outer = [{}, {}]; let iteration = 0;
    capture(() => iteration < outer.length, () => {
      const value = outer[iteration++];
      if (mode !== 'copy-many' && index % 3 === 1) throw value;
      return value;
    }, (get, set) => saved.push({get, set}));
    assert.equal(saved.length, 2);
    for (const [cell, {get, set}] of saved.entries()) {
      assert.equal(get.name, 'get'); assert.equal(get.length, 1);
      for (const payload of [undefined, null, false, 0, NaN, Symbol('payload'), {},
        {get then() { assert.fail('sync generator assimilated thenable'); }}]) {
        const aThis = {}, bThis = {}, a = get.call(aThis, payload), b = get.call(bThis, 'second');
        assert.deepEqual(a.next('ignored'), {value: kind === 'lexical' ? outer[cell] : payload, done: false});
        assert.deepEqual(b.next('ignored'), {value: kind === 'lexical' ? outer[cell] : 'second', done: false});
        const changed = {}; set(changed);
        const bValue = {}, aValue = {}, br = b.next(bValue), ar = a.next(aValue);
        assert.equal(br.done, true); assert.equal(ar.done, true);
        if (kind === 'closure') {
          assert.equal(typeof ar.value, 'function'); assert.equal(ar.value.name, 'read');
          assert.equal(ar.value.length, 0); assert.notEqual(ar.value, br.value);
          assert.equal(ar.value(), aValue); assert.equal(br.value(), bValue);
          set({}); assert.equal(ar.value(), aValue); assert.equal(br.value(), bValue);
        } else if (kind === 'frame-copy') {
          assert.equal(ar.value, aThis); assert.equal(br.value, bThis);
        } else {
          assert.ok(Object.is(ar.value, kind === 'copy' ? changed : payload));
          assert.ok(Object.is(br.value, kind === 'copy' ? changed : 'second'));
        }
        assert.deepEqual(a.next(), {value: undefined, done: true});
        set(outer[cell]);
        for (const initial of [false, true]) for (const kind of ['throw', 'return']) {
          const iterator = get(payload), error = {};
          if (!initial) iterator.next();
          if (kind === 'return') assert.deepEqual(iterator.return(error), {value: error, done: true});
          else assert.throws(() => iterator.throw(error), value => value === error);
          assert.deepEqual(iterator.next(), {value: undefined, done: true});
        }
      }
      const early = get(() => assert.fail('prelude must not invoke argument'));
      assert.deepEqual(early.return('early'), {value: 'early', done: true});
      if (kind === 'frame-copy') for (const receiver of [undefined, null, false, 0, NaN, 'text', Symbol('this'), {}]) {
        const iterator = get.bind(receiver)('payload');
        assert.deepEqual(iterator.next(), {value: 'payload', done: false});
        const result = iterator.next();
        assert.equal(result.done, true); assert.ok(Object.is(result.value, receiver));
      }
    }
  }
}
execFileSync(process.argv[2], ['--yield-prelude-negative'], {encoding: 'utf8', timeout: 60_000});
