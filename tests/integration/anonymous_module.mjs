import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// An independent native ESM model of the decoded fixture. Wire *default*
// never appears here or in the generated identifiers.
async function observe(source, self, initial) {
  const trace = [];
  const context = vm.createContext({trace});
  const root = new vm.SourceTextModule(source, {context});
  const dep = new vm.SourceTextModule(`
    export let value = ${initial}; export default 7;
    export function mutate() {
      if (this !== undefined) throw new Error('receiver');
      trace.push('mutate'); ++value;
    }`, {context});
  const side = new vm.SourceTextModule("trace.push('side');", {context});
  const star = new vm.SourceTextModule("trace.push('star'); export const starValue = 9;", {context});
  await root.link(name => name === './dep.mjs' ? dep : name === './side.mjs' ? side : star);
  assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
  assert.throws(() => root.namespace.answerAlias, {name: 'ReferenceError'});
  await root.evaluate();
  const ns = root.namespace;
  assert.deepEqual(Object.keys(ns), ['answerAlias', 'default', 'depNamespace', 'live', 'snapshot', 'starValue']);
  assert.deepEqual(trace, ['side', 'star', 'mutate']);
  assert.equal(ns.default, ns.answerAlias);
  assert.equal(ns.snapshot, dep.namespace);
  assert.equal(ns.depNamespace, dep.namespace);
  assert.equal(ns.live, initial + 1);
  if (self) {
    assert.equal(ns.default.name, 'reader'); assert.equal(ns.default.length, 0);
    assert.equal(ns.default(), ns.default);
  } else assert.equal(ns.default, initial + 1);
  Reflect.apply(dep.namespace.mutate, undefined, []);
  assert.equal(ns.live, initial + 2);
  assert.equal(ns.snapshot.value, initial + 2);
  assert.equal(ns.default, ns.answerAlias);
  if (self) assert.equal(ns.default(), ns.default);
  else assert.equal(ns.default, initial + 1);
  assert.equal(Reflect.set(ns, 'default', 0), false);
}
for (const profile of ['1202', '1206', '1301']) {
  for (const self of [false, true]) {
    const generated = execFileSync(process.argv[2], [self ? '--anonymous-function' : '--anonymous', profile], {encoding: 'utf8'});
    assert.equal(generated.includes('*default*'), false);
    assert.match(generated, /let \$default\$\$\$0 =/); // User local spelling survives.
    assert.match(generated, /let \$default\$\$\$\$0 =/); // Generated binding does not shadow it.
    const reference = `
      import './side.mjs';
      import {value as input, mutate} from './dep.mjs';
      import unused from './dep.mjs'; import * as ns from './dep.mjs';
      import './star.mjs'; export {value as live} from './dep.mjs';
      export * as depNamespace from './dep.mjs'; export * from './star.mjs';
      let result = input; let snapshot = ns; result = result;
      mutate(); result = input;
      ${self ? 'result = function reader() { return result; };' : ''}
      export {result as default, result as answerAlias, snapshot};`;
    for (const initial of [0, 42, -1]) {
      await observe(generated, self, initial);
      await observe(reference, self, initial);
    }
    // Valid ESM mutation splits two aliases of one VM slot into distinct values.
    const mutation = generated.replace(/(export \{ )([^ ]+)( as "default")/,
      (_, start, name, tail) => `${start}$default$$$0${tail}`);
    assert.notEqual(mutation, generated);
    new vm.SourceTextModule(mutation);
    await assert.rejects(observe(mutation, self, 42));
  }
}
console.log('anonymous modules: 36 independent executions, 6 valid-syntax mutations');
