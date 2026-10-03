import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// Independent native ESM model. Only the encoded ABC reaches the decompiler.
const prelude = `
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
`;
const exports = 'export {answer, answer as answerAlias, snapshot};';
async function setup(source, fail = false, failure = undefined, cycle = false, initial = 42) {
  const trace = [];
  const context = vm.createContext({trace, failure});
  const create = (body, identifier) => new vm.SourceTextModule(body, {context, identifier});
  const root = create(source, './root.mjs');
  const dependency = create(`
    ${cycle ? "import {answer} from './root.mjs'; trace.push(answer);" : ''}
    trace.push('dep'); export let value = ${initial}; export default 7;
    export function mutate() {
      if (this !== undefined) throw new Error('receiver changed');
      trace.push('mutate'); ${fail ? 'throw failure;' : ''} ++value;
    }
  `, './dep.mjs');
  const modules = new Map([
    ['./root.mjs', root], ['./dep.mjs', dependency],
    ['./side.mjs', create("trace.push('side');", './side.mjs')],
    ['./star.mjs', create("trace.push('star'); export const starValue = 9;", './star.mjs')],
  ]);
  await root.link(specifier => modules.get(specifier));
  return {root, dependency, trace};
}
let comparisons = 0;
const modes = ['--module-writer', '--module-writer-branch', '--module-writer-literal', '--module-writer-catch', '--module-writer-parent-branch', '--module-writer-parent-branch-catch'];
for (const profile of ['1202', '1206', '1301']) {
  // The current entry proof deliberately refuses early-created writers whose
  // initialization needs escape/call analysis. Never equate a refused gap with
  // successful source recovery, nor silently turn raw initialization into TDZ.
  execFileSync(process.argv[2], ['--module-writer-early', profile]);
  for (const mode of modes) {
    const generated = execFileSync(process.argv[2], [mode, profile], {encoding:'utf8'});
    const branch = mode === '--module-writer-branch';
    const literal = mode === '--module-writer-literal';
    const caught = mode.endsWith('-catch');
    const parentBranch = mode.startsWith('--module-writer-parent-branch');
    // Pinned JEQZ tests false or numeric zero, without ToBoolean/coercion.
    const assignment = literal ? 'snapshot = 7;' : branch ? 'if (value !== false && value !== 0) snapshot = value;' : 'snapshot = value;';
    const body = caught ? `try { ${assignment} throw value; } catch (error) { return error; }` : `${assignment} return value;`;
    const parentPrelude = parentBranch ? prelude.replace('let answer = input;', 'if (input !== false && input !== 0) mutate();\nlet answer = input;') : prelude;
    const reference = `${parentPrelude} answer = function reader(value) { ${body} }; ${exports}`;
    assert.doesNotMatch(generated, /INCOMPLETE/);
    assert.equal((generated.match(/let snapshot\s*=/g) ?? []).length, 1);
    for (const initial of parentBranch ? [42, 0] : [42]) {
      for (const source of [generated, reference]) {
        const {root, dependency, trace} = await setup(source, false, undefined, false, initial);
        const extraMutation = parentBranch && initial !== 0;
        const expectedTrace = ['side','dep','star', ...(extraMutation ? ['mutate'] : []), 'mutate'];
        assert.throws(() => root.namespace.snapshot, {name:'ReferenceError'});
        await root.evaluate();
        const writer = root.namespace.answer;
        assert.equal(writer, root.namespace.answerAlias);
        assert.equal(writer.name, 'reader'); assert.equal(writer.length, 1);
        assert.equal(root.namespace.snapshot, dependency.namespace);
        const hostile = {get valueOf() {assert.fail('write coerced the value');}, get toString() {assert.fail('write stringified the value');}};
        const callable = () => {};
        const values = [undefined, null, false, true, 0, -0, NaN, Infinity, -Infinity, 3, '', 'text', 0n, 7n, Symbol('identity'), {}, [], callable, hostile, dependency.namespace];
        let previous = dependency.namespace;
        for (const value of values) {
          assert.ok(Object.is(writer(value, hostile), value));
          const written = !branch || (value !== false && value !== 0);
          const expected = written ? literal ? 7 : value : previous;
          assert.ok(Object.is(root.namespace.snapshot, expected));
          assert.equal(root.namespace.answer, writer);
          assert.equal(root.namespace.answerAlias, writer);
          previous = expected;
          if (source === generated) ++comparisons;
        }
        assert.deepEqual(trace, expectedTrace);
        assert.equal(root.namespace.live, initial + 1 + Number(extraMutation));
        assert.equal(Reflect.set(root.namespace, 'snapshot', 99), false);
        Reflect.apply(dependency.namespace.mutate, undefined, []);
        assert.equal(root.namespace.live, initial + 2 + Number(extraMutation));
        assert.ok(Object.is(root.namespace.snapshot, previous));
        const failed = await setup(source, true, hostile, false, initial);
        await assert.rejects(failed.root.evaluate(), error => error === hostile);
        if (extraMutation) {
          assert.throws(() => failed.root.namespace.answer, {name:'ReferenceError'});
          assert.throws(() => failed.root.namespace.snapshot, {name:'ReferenceError'});
        } else {
          assert.equal(failed.root.namespace.answer, initial);
          assert.equal(failed.root.namespace.snapshot, failed.dependency.namespace);
        }
        assert.deepEqual(failed.trace, ['side','dep','star','mutate']);
        const cyclic = await setup(source, false, undefined, true);
        await assert.rejects(cyclic.root.evaluate(), {name:'ReferenceError'});
        assert.throws(() => cyclic.root.namespace.snapshot, {name:'ReferenceError'});
      }
    }
  }
}
console.log(`${comparisons} module writer comparisons: identity, branch, literal, catch, parent control flow, exports, initialization failure and cyclic TDZ passed`);
