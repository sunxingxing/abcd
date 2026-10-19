import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {pathToFileURL} from 'node:url';
const [structure,compiler,input,probe,unit,target,metadataProbe] = process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-sendable-source-'));
function run(exe,args) { const r=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:16*1024*1024});assert.ifError(r.error);assert.equal(r.status,0,r.stderr);return r.stdout; }
try {
  const out=join(dir,'out');run(structure,[input,out,'--value-source','--language-source']);
  const manifest=JSON.parse(readFileSync(join(out,'manifest.json')));
  const record=manifest.records.find(r=>r.source_status!==2);
  assert.equal(record.source_complete,true,JSON.stringify(record));
  assert.equal(record.source_language,'arkts');assert.match(record.source,/\.ets$/);
  const text=readFileSync(join(out,record.source),'utf8');
  assert.match(text,/value\s*:\s*number\s*;/);assert.match(text,/constructor\([^)]*\)\s*\{\s*"use sendable";/);
  const abc=join(dir,'recompiled.abc');
  const emitted=join(dir,'emitted.ets');writeFileSync(emitted,text);
  run(compiler,['--module','--merge-abc','--extension=ts','--opt-level=0','--record-name=sendable_closed',`--output=${abc}`,emitted]);
  // Erase storage-only TS descriptions, not constructor [[Set]] statements.
  // Node proves ordinary-call semantics only, never shared heap/type checks.
  const js=text.replace(/^\s*value\s*:\s*number\s*;\s*$/gm,'');
  const module=join(dir,'closed.mjs');writeFileSync(module,js);
  const {Closed}=await import(pathToFileURL(module));
  assert.equal(Closed.length,1);assert.equal(Closed.name,'Closed');assert.equal(new Closed().read(),undefined);assert.equal(new Closed(undefined).read(),undefined);const a=new Closed(7),b=new Closed(9);
  assert.equal(a.read(),7);assert.equal(b.read(),9);a.value=12;assert.equal(a.read(),12);
  const trace=[];Object.defineProperty(Object.prototype,'value',{configurable:true,set(v){trace.push(v);}});
  try {new Closed(4);assert.deepEqual(trace,[0,4]);}finally{delete Object.prototype.value;}
  const original=JSON.parse(run(probe,[input]));
  assert.deepEqual(JSON.parse(run(probe,[abc])),original);
  function roundtripMutation(label,changed) {
    assert.notEqual(changed,text,label+' mutation located');
    const file=join(dir,label+'.ets'),abc=join(dir,label+'.abc');writeFileSync(file,changed);
    run(compiler,['--module','--merge-abc','--extension=ts','--opt-level=0','--record-name=sendable_closed',`--output=${abc}`,file]);
    assert.notDeepEqual(JSON.parse(run(probe,[abc])),original,label+' mutation must be detected');
  }
  roundtripMutation('missing-directive',text.replace('"use sendable"','"ordinary"'));
  roundtripMutation('wrong-mask',text.replace('value: number','value: string'));
  roundtripMutation('missing-set',text.replace(/\s*this\.value = 0;/,''));
  roundtripMutation('extra-initializer',text.replace('value: number;','value: number = 1;'));
  // Source-oracle differential compares ordinary calls. The field declarations
  // are storage-only ArkTS input, so erase them entirely before Node execution.
  const oracle=join(dir,'oracle.mjs');
  writeFileSync(oracle,`export class Closed { constructor(value) { this.value=0; this.value=value; } read(){return this.value;} }`);
  const Oracle=(await import(pathToFileURL(oracle))).Closed;
  function observe(C,throwAt=0) {
    const trace=[];let count=0;const values=new WeakMap();
    Object.defineProperty(Object.prototype,'value',{configurable:true,
      set(v){trace.push(['set',v]);if(++count===throwAt)throw new RangeError();values.set(this,v);},
      get(){trace.push(['get']);return values.get(this);}});
    try {const a=new C(7);trace.push(['read',a.read()],['length',C.length]);a.value=12;trace.push(['read',a.read()]);}
    catch(e){trace.push(['throw',e.constructor.name]);}
    finally {delete Object.prototype.value;}
    return trace;
  }
  for(const n of [0,1,2,3])assert.deepEqual(observe(Closed,n),observe(Oracle,n));
  const mutated=join(dir,'mutated.mjs');writeFileSync(mutated,js.replace(/\s*this\.value = 0;/,''));
  const Mutant=(await import(pathToFileURL(mutated))).Closed;
  assert.notDeepEqual(observe(Mutant),observe(Oracle),'missing [[Set]] mutation');
  // Production discovery + finished proof on geometric field counts, including
  // the 200/201 boundary. The C++ test finds exact byte/work admission floors.
  for(const n of [200,201,400,800]) {
    const ts=join(dir,'scale'+n+'.ts'),abc=join(dir,'scale'+n+'.abc');
    writeFileSync(ts,`export class Fields { ${Array.from({length:n},(_,i)=>`f${i}: number;`).join('\n')} constructor(){ "use sendable"; } }`);
    run(compiler,['--module','--merge-abc','--extension=ts','--opt-level=0',`--record-name=scale${n}`,`--output=${abc}`,ts]);
    assert.match(run(unit,[abc]),new RegExp(`fields=${n} instance=${n}`));
    const out=join(dir,'scale-out'+n);run(structure,[abc,out,'--value-source','--language-source']);
    const r=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.source_status!==2);
    assert.equal(r.source_complete,true);const code=readFileSync(join(out,r.source),'utf8');
    assert.equal((code.match(/f[0-9]+: number;/g)||[]).length,n);
    const ets=join(dir,'scale-emitted'+n+'.ets'),again=join(dir,'scale-again'+n+'.abc');writeFileSync(ets,code);
    run(compiler,['--module','--merge-abc','--extension=ts','--opt-level=0',`--record-name=scale${n}`,`--output=${again}`,ets]);
    assert.deepEqual(JSON.parse(run(probe,[again])),JSON.parse(run(probe,[abc])));

  }
  // All primitive/union spellings, plus quoted and contextual-keyword storage
  // names. This checks TS grammar and exact frontend mask reconstruction.
  const types=[[1,'number'],[2,'string'],[4,'boolean'],[16,'bigint'],[64,'null'],[128,'undefined']];
  const fields=[];for(let mask=1;mask<256;++mask)if(!(mask&40))fields.push(`f${mask}: ${types.filter(([b])=>b&mask).map(([,s])=>s).join(' | ')};`);
  fields.push('"odd-key": string;','static: boolean;','private: bigint;');
  const ts=join(dir,'types.ts'),typesAbc=join(dir,'types.abc'),typesOut=join(dir,'types-out');
  writeFileSync(ts,`export class Types { ${fields.join('\n')} constructor(){ "use sendable"; } }`);
  run(compiler,['--module','--merge-abc','--extension=ts','--opt-level=0','--record-name=types',`--output=${typesAbc}`,ts]);
  run(structure,[typesAbc,typesOut,'--value-source','--language-source']);
  const tr=JSON.parse(readFileSync(join(typesOut,'manifest.json'))).records.find(r=>r.source_status!==2);
  assert.equal(tr.source_complete,true,JSON.stringify(tr));
  const ets=join(dir,'types-emitted.ets'),typesAgain=join(dir,'types-again.abc');writeFileSync(ets,readFileSync(join(typesOut,tr.source)));
  run(compiler,['--module','--merge-abc','--extension=ts','--opt-level=0','--record-name=types',`--output=${typesAgain}`,ets]);
  assert.deepEqual(JSON.parse(run(probe,[typesAgain])),JSON.parse(run(probe,[typesAbc])));
  const staticFile=join(dir,'static.ts'),staticAbc=join(dir,'static.abc'),staticOut=join(dir,'static-out');
  writeFileSync(staticFile,'export class Static { static extra:number; constructor(){ "use sendable"; } }');
  run(compiler,['--module','--merge-abc','--extension=ts','--opt-level=0',`--output=${staticAbc}`,staticFile]);
  run(structure,[staticAbc,staticOut,'--value-source','--language-source']);
  const staticRefusal=JSON.parse(readFileSync(join(staticOut,'manifest.json'))).records.find(r=>r.source_sendable);
  assert.equal(staticRefusal.source_complete,false);assert.equal(staticRefusal.source_sendable.kind,1);
  // Valid wire-level ModuleExportName strings can lack a spelling in the
  // pinned TS parser. Keep a typed gate; never dereference a concurrent-only
  // witness in a Sendable-only module or publish invalid ArkTS.
  for(const [label,code,from,to] of [
    ['quoted-export','class Closed { value:number; constructor(){ "use sendable"; } } export {Closed as renamed};','renamed','ren-med'],
    ['quoted-import','import {foo as local} from "./dep"; export const copy=local; export class Closed {value:number;constructor(){ "use sendable"; }}','foo','f-o']]) {
    const ts=join(dir,label+'.ts'),abc=join(dir,label+'.abc'),out=join(dir,label+'-out');writeFileSync(ts,code);
    run(compiler,['--module','--merge-abc','--extension=ts','--opt-level=0',`--output=${abc}`,ts]);
    const bytes=readFileSync(abc),pattern=Buffer.from(from+'\0'),at=bytes.indexOf(pattern);
    assert(at>=0);assert.equal(bytes.indexOf(pattern,at+1),-1);Buffer.from(to+'\0').copy(bytes,at);
    const ADLER_MOD=65521;let a=1,b=0;for(let i=12;i<bytes.length;++i){a=(a+bytes[i])%ADLER_MOD;b=(b+a)%ADLER_MOD;}bytes.writeUInt32LE(((b<<16)|a)>>>0,8);writeFileSync(abc,bytes);
    run(structure,[abc,out,'--value-source','--language-source']);
    const refusal=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.source_sendable);
    assert.equal(refusal.source_complete,false);assert.equal(refusal.source_sendable.kind,11,label+' binding spelling');
    assert.equal(refusal.source_sendable.pc,undefined,'module metadata has no instruction PC');
  }
  const generic=join(dir,'generic.ts'),genericAbc=join(dir,'generic.abc'),genericOut=join(dir,'generic-out');
  writeFileSync(generic,'export class G<T> { value:T; constructor(){ "use sendable"; } }');
  run(compiler,['--module','--merge-abc','--extension=ts','--opt-level=0',`--output=${genericAbc}`,generic]);
  run(structure,[genericAbc,genericOut,'--value-source','--language-source']);
  const rejected=JSON.parse(readFileSync(join(genericOut,'manifest.json'))).records.find(r=>r.source_sendable);
  assert.equal(rejected.source_complete,false);assert.equal(rejected.source_sendable.kind,7);assert.equal(rejected.source_sendable.mask,32);
  assert(rejected.source_causes.some(c=>c.domain==='abcd::source::SendableError'&&c.value===7));
  if(target) {
    const targetOut=join(dir,'target-out');run(structure,[target,targetOut,'--value-source','--language-source']);
    const r=JSON.parse(readFileSync(join(targetOut,'manifest.json'))).records.find(r=>r.entity===28934);
    assert.equal(r.source_complete,true);assert.equal(r.source_language,'arkts');assert.match(r.source,/\.ets$/);
    const meta=JSON.parse(readFileSync(join(targetOut,r.metadata)));
    const ets=join(dir,'target.ets'),abc=join(dir,'target.abc');writeFileSync(ets,readFileSync(join(targetOut,r.source)));
    run(compiler,['--module','--merge-abc','--extension=ts','--opt-level=0','--record-name='+meta.descriptor.slice(1,-1),`--output=${abc}`,ets]);
    const before=JSON.parse(run(probe,[target,'28934'])),after=JSON.parse(run(probe,[abc]));
    assert.deepEqual(after,before,'real SendableTest round-trip');
    const c=before[0].classes[0];assert.equal(before[0].shared,false);assert.equal(before[0].sendable_environments,0);assert.equal(c.kind,1);assert.equal(c.length,1);
    assert.equal(c.heritage,false);assert.equal(c.constructor_sendable_flag,false);
    assert.deepEqual(c.fields,[{name:'taskId',mask:1,placement:0}]);assert.equal(c.instance_count,1);
    assert.equal(c.sendable_environments,0);assert.deepEqual(c.sets,['taskId','taskId']);
    assert.deepEqual(c.members,[{name:'getTaskId',kind:0,length:0,placement:0}]);
    assert.deepEqual(before[0].exports,[{slot:0,local:'SendableTest',export:'SendableTest'}]);
    if(metadataProbe) {
      const location=JSON.parse(run(metadataProbe,[target,'29066','9']));
      const bytes=readFileSync(target),mask=location.field_types+10;
      // Pinned literal wire format: count:u32, STRING:u8+entity:u32,
      // INTEGER:u8+mask:u32. Mutate only the mask in a temporary ABC copy.
      assert.equal(bytes.readUInt32LE(mask),1);bytes.writeUInt32LE(256,mask);
      const ADLER_MOD=65521;let a=1,b=0;for(let i=12;i<bytes.length;++i){a=(a+bytes[i])%ADLER_MOD;b=(b+a)%ADLER_MOD;}
      bytes.writeUInt32LE(((b<<16)|a)>>>0,8);
      const broken=join(dir,'bad-mask.abc'),brokenOut=join(dir,'bad-mask-out');writeFileSync(broken,bytes);
      run(structure,[broken,brokenOut,'--value-source','--language-source']);
      const previous=JSON.parse(readFileSync(join(targetOut,'manifest.json'))),changed=JSON.parse(readFileSync(join(brokenOut,'manifest.json')));
      const refusal=changed.records.find(r=>r.entity===28934);
      assert.equal(refusal.source_complete,false);assert.equal(refusal.source_sendable.kind,6);
      assert.equal(refusal.source_sendable.field,0);assert.equal(refusal.source_sendable.mask,256);
      assert.equal(refusal.source_sendable.pc,9);assert.equal(refusal.source_sendable.working_pc,9);
      assert(refusal.source_causes.some(c=>c.domain==='abcd::abc::SendableFieldError'&&c.value===2));
      for(const old of previous.records)if(old.source_complete && old.entity!==28934) {
        const now=changed.records.find(r=>r.entity===old.entity);assert.equal(now.source_complete,true);
        assert.equal(now.source,old.source);assert.deepEqual(readFileSync(join(brokenOut,now.source)),readFileSync(join(targetOut,old.source)));
      }
    }

  }
  console.log('production closed Sendable + exact kind/field/member/module/Set round-trip; Node ordinary-call differential + mutations; 200/201/400/800 and budgets; GENERIC typed refusal');
}finally{rmSync(dir,{recursive:true,force:true});}
