import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const recovered = execFileSync(process.argv[2], ['source'], {encoding: 'utf8'});
// Independent source model of the fixture's module graph; the production
// process receives only the synthetic ABC, never this reference source.
const reference = `
import './side.mjs';
import {value as input, mutate} from './dep.mjs';
import unused from './dep.mjs';
import * as ns from './dep.mjs';
import './star.mjs';
export {value as live} from './dep.mjs';
export * as depNamespace from './dep.mjs';
export * from './star.mjs';
let answer = input;
let snapshot = ns;
answer = answer;
mutate();
answer = input;
export {answer, answer as answerAlias, snapshot};
`;
async function setup(source, {cycle = false, defaultExport = true, initial = 42} = {}) {
  const trace = [];
  const context = vm.createContext({trace});
  const create = (body, identifier) => new vm.SourceTextModule(body, {context, identifier});
  const root = create(source, './root.mjs');
  const dependency = create(`
    ${cycle ? "import {answer} from './root.mjs'; trace.push(answer);" : ''}
    trace.push('dep'); export let value = ${initial};
    export function mutate() {
      if (this !== undefined) throw new Error('receiver changed');
      trace.push('mutate'); ++value;
    }
    ${defaultExport ? 'export default 7;' : ''}
  `, './dep.mjs');
  const modules = new Map([
    ['./root.mjs', root], ['./dep.mjs', dependency],
    ['./side.mjs', create("trace.push('side');", './side.mjs')],
    ['./star.mjs', create("trace.push('star'); export const starValue = 9; export const live = -1; export default 0;", './star.mjs')],
  ]);
  await root.link(specifier => {
    assert.ok(modules.has(specifier), specifier);
    return modules.get(specifier);
  });
  return {root, dependency, trace};
}
async function observe(source, conditional = false, initial = 42) {
  const {root, dependency, trace} = await setup(source, {initial});
  const mutations = 1 + Number(conditional && initial !== 0);
  const final = initial + mutations;
  assert.throws(() => root.namespace.answer, {name: 'ReferenceError'});
  await root.evaluate();
  const ns = root.namespace;
  assert.deepEqual(trace, ['side', 'dep', 'star', ...Array(mutations).fill('mutate')]);
  assert.deepEqual(Object.keys(ns), ['answer', 'answerAlias', 'depNamespace', 'live', 'snapshot', 'starValue']);
  assert.equal(ns.answer, final); assert.equal(ns.answerAlias, final); assert.equal(ns.live, final);
  assert.equal(ns.starValue, 9);
  assert.equal(ns.snapshot, dependency.namespace);
  assert.equal(ns.depNamespace, dependency.namespace);
  assert.equal(Reflect.set(ns, 'answer', 0), false);
  Reflect.apply(dependency.namespace.mutate, undefined, []);
  assert.equal(ns.live, final + 1); assert.equal(ns.snapshot.value, final + 1);
  assert.equal(ns.answer, final); assert.equal(ns.answerAlias, final);
  return {trace, answer: ns.answer, live: ns.live, keys: Object.keys(ns)};
}
assert.deepEqual(await observe(recovered), await observe(reference));
for (const source of [recovered, reference]) {
  const {root} = await setup(source, {cycle: true});
  await assert.rejects(root.evaluate(), {name: 'ReferenceError'});
  // Even unused imports retain their link-time export obligation.
  await assert.rejects(setup(source, {defaultExport: false}), {name: 'SyntaxError'});
}

const conditional = execFileSync(process.argv[2], ['source', 'branch'], {encoding: 'utf8'});
const conditionalReference = reference.replace('let answer = input;', 'if (input !== 0) mutate();\nlet answer = input;');
for (const initial of [0, 42])
  assert.deepEqual(await observe(conditional, true, initial), await observe(conditionalReference, true, initial));
const cyclic = await setup(conditional, {cycle: true});
await assert.rejects(cyclic.root.evaluate(), {name: 'ReferenceError'});
