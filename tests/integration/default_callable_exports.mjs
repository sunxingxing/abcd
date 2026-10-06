import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const [recordTool,familyTool] = process.argv.slice(2);
async function load(source,context,linker = () => {throw Error('unexpected import');}) {
  const module = new vm.SourceTextModule(source,{context});
  await module.link(linker);
  // A retained expression initializes at evaluation, not at linking as an
  // export-default function declaration would. Do not use source text as proof.
  assert.throws(() => module.namespace.default,{name:'ReferenceError'});
  await module.evaluate();
  const value = module.namespace.default; await module.evaluate(); assert.equal(module.namespace.default,value);
  assert.equal(Reflect.set(module.namespace,'default',null),false);
  return module;
}
let cases = 0, mutations = 0;
const payloads = [undefined,null,-0,NaN,1n,Symbol('value'),{}];
const rows = execFileSync(familyTool,['--default-names'],{encoding:'utf8'}).trim().split('\n').map(JSON.parse);
for (const {callable,wanted,source} of rows) {
  assert.match(source,/export default \(/); assert.doesNotMatch(source,/let \$default/);
  const functionText = callable === 'function' ? 'function(arg) {return arg;}' :
    callable === 'arrow' ? '(arg) => {return arg;}' : callable === 'async-function' ? 'async function(arg) {return await arg;}' :
    callable === 'async-arrow' ? 'async (arg) => {return await arg;}' : callable === 'generator' ?
      'function*(arg) {return yield arg;}' : 'async function*(arg) {return yield arg;}';
  // Independent native expressions model the original runtime name without an
  // immutable function-self binding. Parentheses preserve default inference.
  const reference = wanted === 'default' ? `export default (${functionText});` :
    `export default ({[${JSON.stringify(wanted)}]: ${functionText}})[${JSON.stringify(wanted)}];`;
  for (const value of payloads) {
    const observe = async text => {
      const module = await load(text,vm.createContext({}));
      const F = module.namespace.default;
      assert.equal(F.name,wanted); assert.equal(F.length,1);
      const receiver = {};
      if (callable.includes('generator')) {
        const iterator = Reflect.apply(F,receiver,[value]);
        const first = await iterator.next(); assert.equal(first.value,value); assert.equal(first.done,false);
        const last = await iterator.next(receiver); assert.equal(last.value,receiver); assert.equal(last.done,true);
        const thrown = Symbol('original throw'), failing = Reflect.apply(F,null,[value]);
        await failing.next();
        if (callable === 'async-generator') await assert.rejects(failing.throw(thrown),error => error === thrown);
        else assert.throws(() => failing.throw(thrown),error => error === thrown);
      } else assert.ok(Object.is(await Reflect.apply(F,receiver,[value]),value));
      if (callable === 'function') {
        const instance = Reflect.construct(F,[value]);
        assert.equal(instance === value,typeof value === 'object' && value !== null);
        if (instance !== value) assert.equal(Object.getPrototypeOf(instance),F.prototype);
      } else assert.throws(() => Reflect.construct(F,[value]),{name:'TypeError'});
      return {name:F.name,length:F.length,prototype:Object.hasOwn(F,'prototype')};
    };
    assert.deepEqual(await observe(source),await observe(reference)); ++cases;
  }
  if (wanted === 'default' && callable === 'function') {
    const altered = source.replace('export default (function','export default function').replace('});','};');
    assert.notEqual(altered,source); new vm.SourceTextModule(altered);
    await assert.rejects(load(altered,vm.createContext({})),assert.AssertionError); ++mutations;
  }
}
for (const profile of ['1202','1206','1301']) for (const kind of ['function','class']) {
  const source = execFileSync(recordTool,[`--direct-default-${kind}`,profile],{encoding:'utf8'});
  assert.match(source,/export default \(/); assert.doesNotMatch(source,/let \$default\$\$\$\$0/);
  const imports = `import './side.mjs'; import {value as input} from './dep.mjs';
    import {mutate} from './dep.mjs'; import unused from './dep.mjs'; import * as ns from './dep.mjs'; import './star.mjs';
    export {value as live} from './dep.mjs'; export * as depNamespace from './dep.mjs'; export * from './star.mjs';
    let snapshot = ns; export {snapshot};`;
  const reference = imports + (kind === 'function' ? 'export default (function() {return input;});' :
    'export default (class Box {constructor() {}});');
  for (const initial of payloads) {
    const observe = async text => {
      const trace = [], globalBox = {};
      const context = vm.createContext({initial,trace,Box:globalBox});
      const dep = new vm.SourceTextModule('export let value = initial; export default 7; export function mutate() {value = "changed";}',{context});
      const side = new vm.SourceTextModule('trace.push("side");',{context});
      const star = new vm.SourceTextModule('trace.push("star"); export const starValue = 9;',{context});
      const module = await load(text + '\nexport function readBox() {return Box;}',context,
        name => name === './dep.mjs' ? dep : name === './side.mjs' ? side : star);
      assert.deepEqual(trace,['side','star']); assert.equal(module.namespace.readBox(),globalBox);
      const F = module.namespace.default;
      assert.equal(F.name,kind === 'function' ? 'default' : 'Box'); assert.equal(F.length,0);
      assert.equal(module.namespace.snapshot,dep.namespace); assert.equal(module.namespace.depNamespace,dep.namespace);
      assert.deepEqual(Object.keys(module.namespace),['default','depNamespace','live','readBox','snapshot','starValue']);
      if (kind === 'function') {
        assert.ok(Object.is(F(),initial)); dep.namespace.mutate(); assert.equal(F(),'changed');
      } else {
        assert.throws(() => Reflect.apply(F,null,[]),{name:'TypeError'});
        const instance = new F(); assert.deepEqual(Reflect.ownKeys(instance),[]);
        assert.equal(Object.getPrototypeOf(instance),F.prototype); dep.namespace.mutate();
      }
      assert.equal(module.namespace.live,'changed'); assert.equal(module.namespace.default,F);
      return {name:F.name,length:F.length};
    };
    assert.deepEqual(await observe(source),await observe(reference)); ++cases;
  }
  if (kind === 'class') {
    const altered = source.replace('export default ((class Box','export default class Box').replace('}));','};');
    assert.notEqual(altered,source); new vm.SourceTextModule(altered);
    const globalBox = {}, context = vm.createContext({Box:globalBox});
    const dependency = new vm.SourceTextModule('export const value = 1; export const mutate = 0; export default 7;',{context});
    const module = await load(altered + '\nexport function readBox() {return Box;}',context,() => dependency);
    assert.notEqual(module.namespace.readBox(),globalBox); ++mutations;
  }
}
console.log(JSON.stringify({defaultCallableCases:cases,mutations,decodedProfiles:3,fullCorpusAcceptance:false}));
