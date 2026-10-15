import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {copyFileSync,mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure,probe,fixture,compiler]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-concurrent-async-'));
const run=(exe,args,status=0)=>{const p=spawnSync(exe,args,{encoding:'utf8',timeout:180000,maxBuffer:64*1024*1024});assert.ifError(p.error);assert.equal(p.status,status,p.stdout+'\n'+p.stderr);return p.stdout;};
const inspect=file=>JSON.parse(run(probe,[file]));
try {
 const out=join(dir,'out');run(structure,[join(fixture,'functions.abc'),out,'--value-source','--language-source']);
 const record=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.source_status!==2);
 assert.equal(record.source_status,0,JSON.stringify(record));
 const source=readFileSync(join(out,record.source),'utf8');
 assert.equal((source.match(/"use concurrent";/g)||[]).length,6);assert.equal((source.match(/async function/g)||[]).length,6);
 const before=inspect(join(fixture,'functions.abc')).find(r=>r.functions.length);assert.equal(before.functions.length,6);
 const emitted=join(dir,'emitted.js');writeFileSync(emitted,source);
 const abc=join(dir,'emitted.abc');run(compiler,['--module','--merge-abc','--opt-level=0',`--record-name=${before.record.slice(1,-1)}`,'--source-file=functions.js',`--output=${abc}`,emitted]);
 const after=inspect(abc).find(r=>r.record===before.record);assert.ok(after);
 for(const key of ['requests','request_phases','imports','namespace_imports','exports','indirect_exports','star_exports'])assert.deepEqual(after[key],before[key],key);
 const summarize=f=>({name:f.name,kind:f.kind,sendable:f.sendable,annotation_present:f.annotation_present,requests:f.requests,async_protocol:f.async_protocol});
 assert.deepEqual(after.functions.map(summarize).sort((a,b)=>a.name.localeCompare(b.name)),before.functions.map(summarize).sort((a,b)=>a.name.localeCompare(b.name)));
 for(const f of [...before.functions,...after.functions]) {
  assert.equal(f.kind,7);assert.equal(f.sendable,false);assert.equal(f.async_protocol,true);
  assert.equal(f.completion_verified,true);assert.equal(f.async_owner_verified,true);
  assert.ok(f.resolve_ports>=1);assert.equal(f.reject_ports,1);assert.equal(f.notifications,f.resolve_ports+f.reject_ports);
 }
 const erased=join(dir,'erased.js'),erasedAbc=join(dir,'erased.abc');writeFileSync(erased,source.replaceAll('"use concurrent";',''));
 run(compiler,['--module','--merge-abc','--opt-level=0','--record-name=erased',`--output=${erasedAbc}`,erased]);assert.equal(inspect(erasedAbc).find(r=>r.record==='Lerased;').functions.length,0);
 writeFileSync(join(dir,'package.json'),'{"type":"module"}');copyFileSync(join(fixture,'dependency.js'),join(dir,'dependency.js'));
 const original=join(dir,'original.mjs'),recovered=join(dir,'recovered.mjs');copyFileSync(join(fixture,'functions.js'),original);writeFileSync(recovered,source);
 const harness=join(dir,'harness.mjs');writeFileSync(harness,`
 const m=await import(process.argv[2]);const trace=[],values=[];
 async function test(name,input) {if(input instanceof Promise)input.catch(()=>{});trace.push('call:'+name);const p=m[name](input,trace);trace.push('returned:'+String(p instanceof Promise));queueMicrotask(()=>trace.push('micro:'+name));try{values.push(await p)}catch(e){values.push(e.constructor.name)}trace.push('complete:'+name)}
 await test('success',3);await test('after',4);await test('rejected',Promise.reject(new SyntaxError('rejected')));await test('returned',Promise.resolve(9));await test('returned',{then(resolve){trace.push('thenable');resolve('thenable-value')}});await test('returned',Promise.reject(new EvalError('promise')));await test('handled',4);await test('handled',Promise.reject(new URIError('caught')));
 trace.push('call:before');const p=m.before(trace);trace.push('returned:'+String(p instanceof Promise));queueMicrotask(()=>trace.push('micro:before'));try{await p}catch(e){values.push(e.constructor.name)}
 console.log(JSON.stringify({values,trace}));`);
 const expected=run(process.execPath,[harness,original]);assert.equal(run(process.execPath,[harness,recovered]),expected);
 const mutation=join(dir,'mutation.mjs');writeFileSync(mutation,source.replace('return $arg0;', 'return 31;'));
 assert.notEqual(run(process.execPath,[harness,mutation]),expected,'wrong Promise resolution mutation');
 run(process.execPath,['--check',recovered]);
 // Preserve the existing catch-input gate. No liveness/exception proof is
 // weakened to make a finally-around-await fixture publish in this round.
 const cleanup=join(dir,'cleanup');run(structure,[join(fixture,'finally.abc'),cleanup,'--value-source','--language-source']);
 const c=JSON.parse(readFileSync(join(cleanup,'manifest.json'))).records.find(r=>r.source_status!==2);assert.equal(c.source_status,3);
 assert.equal(c.source_failure.async.check,20);assert.equal(c.source_failure.causes.at(-1).domain,'abcd::analysis::AwaitCatchError');
 assert.ok(inspect(join(fixture,'finally.abc')).find(r=>r.functions.length).functions.every(f=>f.completion_verified));
 console.log('6 async concurrent functions: exact kind/annotation/module table round trip; raw Promise/resolve/reject notify ports; Node resolution, rejection and microtask traces; mutations. Finally-await stays at existing catch-input proof. Taskpool unverified.');
} finally {rmSync(dir,{recursive:true,force:true});}
