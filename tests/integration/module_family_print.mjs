import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
const source = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8'});
const suffix = '\nexport {state, reader, receiver};';
const generated = new vm.SourceTextModule(source + suffix);
const reference = new vm.SourceTextModule(`
  let state = 1;
  let reader = function() { return state; };
  let receiver = function() { return this; };
  state = 42;
  export {state, reader, receiver};
`);
for (const module of [generated, reference]) {
  await module.link(() => { throw Error('unexpected import'); });
  await module.evaluate();
  assert.equal(module.namespace.state, 42);
  assert.equal(module.namespace.reader(), 42);
  const receiver = {};
  assert.equal(module.namespace.receiver.call(receiver), receiver);
  assert.equal(module.namespace.receiver.call(undefined), undefined);
}

for (const mode of ['--generator', '--async']) {
  const nested = execFileSync(process.argv[2], [mode], {encoding: 'utf8'});
  const module = new vm.SourceTextModule(nested + suffix);
  await module.link(() => { throw Error('unexpected import'); });
  await module.evaluate();
  assert.equal(module.namespace.reader(), 42);
  const receiver = {};
  if (mode === '--generator') {
    const iterator = module.namespace.receiver.call(receiver);
    assert.deepEqual(iterator.next(), {value: receiver, done: false});
    assert.deepEqual(iterator.next(99), {value: 99, done: true});
    const error = {};
    const throwing = module.namespace.receiver.call(receiver);
    throwing.next();
    assert.throws(() => throwing.throw(error), e => e === error);
  } else {
    assert.equal(await module.namespace.receiver.call(receiver), receiver);
    const error = {};
    await assert.rejects(module.namespace.receiver.call({then(_, reject) { reject(error); }}), e => e === error);
  }
}

// A discarded module return still evaluates its assignment, even when the
// compound expression's final value is undefined.
const effectful = execFileSync(process.argv[2], ['--undefined-result'], {encoding:'utf8'});
const compound = new vm.SourceTextModule(effectful + suffix);
await compound.link(() => { throw Error('unexpected import'); });
await compound.evaluate();
assert.equal(compound.namespace.state,42);
assert.equal(compound.namespace.reader(),42);
