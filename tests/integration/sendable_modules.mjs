import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join,dirname} from 'node:path';
import {tmpdir} from 'node:os';
import {pathToFileURL} from 'node:url';
const [structure,compiler,input,sharedInput,probe,unit,semantic,target]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-sendable-modules-'));
const run=(exe,args,status=0)=>{const r=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});assert.ifError(r.error);assert.equal(r.status,status,r.stderr);return r.stdout;};
const compile=(path,name)=>{const abc=join(dir,name.replaceAll('/','_')+'.abc');run(compiler,['--module','--merge-abc','--extension=ts','--opt-level=0','--record-name='+name,'--output='+abc,path]);return abc;};
function exported(abc,label,entity){const out=join(dir,label);run(structure,[abc,out,'--value-source','--language-source']);const m=JSON.parse(readFileSync(join(out,'manifest.json')));const r=m.records.find(r=>entity?r.entity===entity:r.source_status!==2);assert.equal(r.source_complete,true,JSON.stringify(r));assert.equal(r.source_language,'arkts');assert.match(r.source,/\.ets$/);return {r,text:readFileSync(join(out,r.source),'utf8'),meta:JSON.parse(readFileSync(join(out,r.metadata)))};}
const proof=(abc,record)=>JSON.parse(run(probe,record?[abc,String(record)]:[abc]));
const erase=s=>s.replace(/^\s*(?:lock_|books_)\s*:\s*library\.[A-Za-z.]+\s*;\s*$/gm,'');
try {
  const base=exported(input,'plain'),shared=exported(sharedInput,'shared');
  assert.match(shared.text,/^"use shared";/);assert(!base.text.includes('"use shared"'));
  for(const [abc,result,name] of [[input,base,'cache'],[sharedInput,shared,'shared']]){
    assert.match(result.text,/lock_: library\.Lock;/);assert.match(result.text,/books_: library\.Store;/);
    const file=join(dir,name+'.ets');writeFileSync(file,result.text);const again=compile(file,name);assert.deepEqual(proof(again),proof(abc));
    const details=proof(abc)[0];assert.equal(details.shared,name==='shared');assert.equal(details.common_js,false);assert.equal(details.tla,false);assert.equal(details.sendable_environments,0);
    const c=details.classes[0];assert.deepEqual(c.fields.map(f=>f.mask),[32,32]);assert.deepEqual(c.constructor_body.reads.map(r=>[r.domain,r.slot,r.guard]),[[1,0,true],[1,0,true]]);
    assert.equal(c.member_bodies.length,3);for(const m of c.member_bodies){assert.equal(m.async_enter,1);assert.equal(m.callbacks.length,1);assert.equal(m.reject,1);}
  }
  // Ordinary-call source oracle only. Locks/shared heap and field enforcement
  // are Ark runtime gaps, not simulated by this small deterministic dependency.
  writeFileSync(join(dir,'dependency.mjs'),`export const trace=[]; export let mode='';export function reset(m){trace.length=0;mode=m;}
    class Lock {constructor(){trace.push('newLock');if(mode==='lock')throw new TypeError();}async run(f){trace.push('run');await 0;trace.push('callback');return f();}}
    class Store {constructor(){trace.push('newStore');this.values=new Map();}put(k,v){trace.push(['put',k,v]);if(mode==='put')throw new RangeError();this.values.set(k,v);}get(k){trace.push(['get',k]);if(mode==='get')throw new SyntaxError();return this.values.get(k);}keys(){trace.push('keys');return [...this.values.keys()];}}
    let library={get Lock(){trace.push('LockGetter');if(mode==='getter')throw new ReferenceError();return Lock;},Store};
    export {library as default};export function replace(){library={get Lock(){trace.push('replacedLockGetter');return Lock;},Store};}`);
  const source=readFileSync(join(dirname(input),'cache.ts'),'utf8');
  writeFileSync(join(dir,'oracle.mjs'),erase(source));writeFileSync(join(dir,'result.mjs'),erase(base.text));
  run(process.execPath,['--check',join(dir,'result.mjs')]);
  const {Cache:Oracle}=await import(pathToFileURL(join(dir,'oracle.mjs'))),{Cache:Actual}=await import(pathToFileURL(join(dir,'result.mjs')));
  const dep=await import(pathToFileURL(join(dir,'dependency.mjs')));
  async function observe(C,mode){dep.reset(mode);const t=dep.trace;try{const c=new C();t.push(['length',C.length]);const p=c.put('a',4);t.push('after-put');t.push(['put-result',await p]);t.push(['get-result',await c.get('a')]);t.push(['keys-result',await c.keys()]);}catch(e){t.push(['throw',e.constructor.name]);}return structuredClone(t);}
  for(const mode of ['', 'getter','lock','put','get'])assert.deepEqual(await observe(Actual,mode),await observe(Oracle,mode));
  dep.replace();assert.deepEqual(await observe(Actual,''),await observe(Oracle,''));
  // Shared module initialization and exported identity remain observable even
  // when Node treats the Ark directive as an ordinary directive string.
  writeFileSync(join(dir,'shared-oracle.mjs'),erase(readFileSync(join(dirname(sharedInput),'shared.ts'),'utf8')));
  writeFileSync(join(dir,'shared-result.mjs'),erase(shared.text));
  const sharedTrace=async file=>{dep.reset('');const m=await import(pathToFileURL(join(dir,file)));const initial=structuredClone(dep.trace);assert(m.cache instanceof m.Cache);await m.cache.put('shared',9);assert.equal(await m.cache.get('shared'),9);assert.equal((await import(pathToFileURL(join(dir,file)))).cache,m.cache);return [initial,structuredClone(dep.trace)];};
  assert.deepEqual(await sharedTrace('shared-result.mjs'),await sharedTrace('shared-oracle.mjs'));
  for(const [label,text] of [['not-shared',shared.text.replace('\"use shared\";','')],['wrong-export',shared.text.replace(' as cache',' as other')]]){
    const file=join(dir,label+'.ets');writeFileSync(file,text);assert.notDeepEqual(proof(compile(file,label)),proof(sharedInput),label);
  }
  const mutated=erase(base.text).replace(/\(this\.books_ = new library\.Store\(\)\);/,'');writeFileSync(join(dir,'mutant.mjs'),mutated);
  const {Cache:Mutant}=await import(pathToFileURL(join(dir,'mutant.mjs')));assert.notDeepEqual(await observe(Mutant,''),await observe(Oracle,''));
  // Actual source mutations must change the independent ABC contract.
  const original=proof(input);
  for(const [label,text] of [['directive',base.text.replace('"use sendable"','"ordinary"')],['mask',base.text.replace('lock_: library.Lock','lock_: number')],['store',base.text.replace(/\(this\.books_ = new library\.Store\(\)\);/,'')]]){
    const file=join(dir,label+'.ets');writeFileSync(file,text);assert.notDeepEqual(proof(compile(file,label)),original,label);
  }
  // A direct imported constructor is the depth-zero form of the same proof.
  const direct=join(dir,'direct.ts');writeFileSync(direct,`import Lock from './dependency.mjs';export class Direct { value:Lock; constructor(){"use sendable";this.value=new Lock();} }`);
  const directAbc=compile(direct,'direct'),directResult=exported(directAbc,'direct-out');
  assert.match(directResult.text,/value: Lock;/);const directEts=join(dir,'direct.ets');writeFileSync(directEts,directResult.text);
  assert.deepEqual(proof(compile(directEts,'direct-again')),proof(directAbc));
  // GENERIC is never arbitrary any/Object or a guessed import.
  const unknown=join(dir,'unknown.ts');writeFileSync(unknown,'export class Unknown<T>{ value:T;constructor(){"use sendable";}}');
  const unknownAbc=compile(unknown,'unknown'),out=join(dir,'unknown-out');run(structure,[unknownAbc,out,'--value-source','--language-source']);
  const refusal=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.source_sendable);assert.equal(refusal.source_sendable.kind,7);assert.equal(refusal.source_sendable.mask,32);
  for(const n of [200,201,400,800]){
    const file=join(dir,'scale'+n+'.ts');writeFileSync(file,`import library from './dependency.mjs';export class Fields {${Array.from({length:n},(_,i)=>`f${i}: library.Lock;`).join('\n')}constructor(){"use sendable";${Array.from({length:n},(_,i)=>`this.f${i}=new library.Lock();`).join('\n')}}}`);
    const abc=compile(file,'scale'+n);assert.match(run(unit,[abc]),new RegExp('types='+n+' budget='));
    const result=exported(abc,'scale-out'+n);const ets=join(dir,'scale-out'+n+'.ets');writeFileSync(ets,result.text);assert.deepEqual(proof(compile(ets,'scale-again'+n)),proof(abc));
  }
  if(target){
    const result=exported(target,'target',24892);assert.match(result.text,/^"use shared";/);
    const ets=join(dir,'LruCache.ets');writeFileSync(ets,result.text);const abc=compile(ets,result.meta.descriptor.slice(1,-1));
    assert.deepEqual(proof(abc),proof(target,24892));
    const t=proof(target,24892)[0];assert.equal(t.shared,true);assert.equal(t.common_js,false);assert.equal(t.tla,false);assert.equal(t.classes[0].length,0);assert.deepEqual(t.classes[0].fields.map(f=>[f.name,f.mask]),[['lock_',32],['books_',32]]);
    const cli=JSON.parse(run(semantic,[target,'25192','--record-source','--language-source']));assert.equal(cli.source_language,'arkts');assert.equal(cli.source_failure,undefined);
  }
  console.log('sendable domain/guard, async member captures, GENERIC spelling, shared table and actual round trips; Node ordinary-call differential; 200/201/400/800 budgets and mutations passed');
}finally{rmSync(dir,{recursive:true,force:true});}
