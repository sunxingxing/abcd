// Original/constructed JS is an oracle only. No source is passed to the engine.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {createRequire} from 'node:module';
const acorn=createRequire(import.meta.url)('internal/deps/acorn/acorn/dist/acorn');
const [original,emitted,kind]=process.argv.slice(2);
let cases=0;
function source(path){
  let text=readFileSync(path,'utf8');
  // Token-based Ark lazy-import syntax adaptation. All supplied dependencies
  // are initialized; this oracle does not measure lazy initialization timing.
  const tokens=[...acorn.tokenizer(text,{ecmaVersion:'latest',sourceType:'module'})],edits=[];
  for(let i=0;i+1<tokens.length;++i)if(tokens[i].type.label==='import'&&tokens[i+1].value==='lazy')edits.push(tokens[i+1]);
  for(const t of edits.reverse())text=text.slice(0,t.start)+text.slice(t.end);
  return text;
}
async function load(text){
  const context=vm.createContext({}),dependencies=new Map();
  const module=new vm.SourceTextModule(text,{context});
  await module.link(name=>{
    const index=Number(name.match(/m(\d+)/)?.[1]);assert.ok(Number.isInteger(index),name);
    const d=new vm.SyntheticModule(['x'],function(){this.setExport('x',index);},{context});dependencies.set(index,d);return d;
  });await module.evaluate();return {ns:module.namespace,dependencies};
}
const a=await load(source(emitted)),b=await load(source(original));
const normalize=x=>Array.isArray(x)?Array.from(x,normalize):x;
const call=(fn,args)=>{try{return {value:normalize(fn(...args))};}catch(e){return {error:[e.name,e.message]};}};
if(kind==='property-wide'){
  const run=(ns,fail)=>{const log=[],saved={};const key={[Symbol.toPrimitive](){log.push('key');if(fail==='key')throw new SyntaxError('key');return 'selected';}};
    const o=new Proxy(saved,{get(t,k){log.push(['get',k]);if(fail==='get')throw new RangeError('get');return t[k];},set(t,k,v){log.push(['set',k,v]);if(fail==='set')throw new TypeError('set');t[k]=v;return true;}});
    return {result:call(ns.read,[o,key,9]),log};};
  for(const fail of ['none','get','set','key']){assert.deepEqual(run(a.ns,fail),run(b.ns,fail));++cases;}
}else if(kind.startsWith('super-')){
  const run=(ns,fail)=>{const log=[],receiver={marker:9,p:'wrong-base'};let n=0;
    const proto={get p(){log.push(['get',this===receiver]);if(++n===fail)throw new RangeError('getter');return this.marker;}};
    Object.setPrototypeOf(ns.B.prototype,proto);
    return {result:call(ns.B.prototype.m.bind(receiver),[]),log};};
  for(const fail of [0,1,75,151]){assert.deepEqual(run(a.ns,fail),run(b.ns,fail));++cases;}
  const before=source(emitted),after=before.replaceAll('super[','this[').replaceAll('super.p','this.p');
  assert.notEqual(before,after,'mutation must change a super reference');
  const mutant=await load(after);assert.notDeepEqual(run(mutant.ns,0),run(b.ns,0));
}else if(kind.startsWith('module-')){
  const read=x=>kind.startsWith('module-sendable')?new x.ns.C().m():x.ns.read();
  assert.deepEqual(normalize(read(a)),normalize(read(b)));++cases;
  for(const [id,value] of [[259,undefined],[258,5n],[257,null]]){
    if(!a.dependencies.size)break;
    a.dependencies.get(id).setExport('x',value);b.dependencies.get(id).setExport('x',value);
    assert.deepEqual(normalize(read(a)),normalize(read(b)));++cases;
  }
  if(a.dependencies.size){const before=normalize(read(a));a.dependencies.get(259).setExport('x','wrong');assert.notDeepEqual(normalize(read(a)),before);}
 }else if(kind==='method-accessor'){
  const run=ns=>{const trace=[];const object={get p(){trace.push('p');return 1;}};const C=ns.f(object),instance=new C();instance.p=99;return {value:instance.p,trace,descriptor:Object.getOwnPropertyDescriptor(C.prototype,'p').enumerable};};
  assert.deepEqual(run(a.ns),run(b.ns));++cases;
  for(const fail of [1,75,150]){const run=ns=>{let n=0;const trace=[];const object={get p(){trace.push(++n);if(n===fail)throw new RangeError('get');}};return {result:call(ns.f,[object]),trace};};assert.deepEqual(run(a.ns),run(b.ns));++cases;}
 }else if(kind==='iterator-return'){
  const run=(ns,values,closeFail,nextFail)=>{const trace=[];let index=0;const iterable={[Symbol.iterator](){trace.push('iterator');return {next(){trace.push(['next',index]);if(index===nextFail)throw new RangeError('next');return index<values.length?{value:values[index++],done:false}:{done:true};},return(){trace.push('close');if(closeFail)throw new TypeError('close');return {done:true};}};}};const object={get p(){trace.push('prefix');return 1;}};return {result:call(ns.f,[object,iterable]),trace};};
  for(const values of [[],[0],[0,2],[1,2]])for(const closeFail of [false,true])for(const nextFail of [-1,0,1]){assert.deepEqual(run(a.ns,values,closeFail,nextFail),run(b.ns,values,closeFail,nextFail));++cases;}
 }else if(kind==='bigint'){
  assert.deepEqual(normalize(a.ns.values()),normalize(b.ns.values()));++cases;
  for(const x of [0n,1n,-1n,10n,123456789012345678901234567890n,0,NaN,null,undefined,Symbol('x')])for(const name of ['add','divide']){
    assert.deepEqual(call(a.ns[name],[x]),call(b.ns[name],[x]));++cases;
  }
  for(const name of ['add','divide']){
    const run=ns=>{const trace=[];const value={[Symbol.toPrimitive](){trace.push('convert');throw new URIError('convert');}};return {result:call(ns[name],[value]),trace};};
    assert.deepEqual(run(a.ns),run(b.ns));++cases;
  }
  const before=source(emitted),after=before.replace('123n','124n');assert.notEqual(before,after);const mutant=await load(after);assert.notDeepEqual(call(mutant.ns.add,[1n]),call(b.ns.add,[1n]));
}else assert.fail('unhandled fixture oracle');
// Node syntax check uses exactly the evaluated adaptation, including ArkTS's
// lazy modifier removal. Raw valid-JS output is checked separately when possible.
const dir=mkdtempSync(join(tmpdir(),'abcd-isa-syntax-'));try{const file=join(dir,'output.mjs');writeFileSync(file,source(emitted));execFileSync(process.execPath,['--check',file]);}finally{rmSync(dir,{recursive:true,force:true});}
console.log(JSON.stringify({cases}));
