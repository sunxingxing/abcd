import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {copyFileSync, mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure,probe,fixture,compiler]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-concurrent-functions-'));
const run=(exe,args,status=0)=>{const r=spawnSync(exe,args,{encoding:'utf8',timeout:180000,maxBuffer:64*1024*1024});assert.ifError(r.error);assert.equal(r.status,status,r.stdout+'\n'+r.stderr);return r.stdout;};
const inspect=input=>JSON.parse(run(probe,[input]));
try {
 const out=join(dir,'output');run(structure,[join(fixture,'functions.abc'),out,'--value-source','--language-source']);
 const manifest=JSON.parse(readFileSync(join(out,'manifest.json')));
 const record=manifest.records.find(r=>r.source_status===0);assert.ok(record);
 const source=readFileSync(join(out,record.source),'utf8');assert.equal((source.match(/"use concurrent";/g)||[]).length,6);
 const before=inspect(join(fixture,'functions.abc')).find(r=>r.record==='Lconcurrent_functions;');
 assert.equal(before.functions.length,6);assert.ok(before.functions.some(f=>f.handlers>0));
 for(const f of before.functions) {assert.equal(f.kind,7);assert.equal(f.sendable,false);assert.equal(f.annotation_present,true);assert.ok(f.notifications>=1);}
 const compiledSource=join(dir,'recompiled.js');writeFileSync(compiledSource,source);
 const recompiled=join(dir,'recompiled.abc');run(compiler,['--module','--merge-abc','--opt-level=0','--record-name=concurrent_functions','--source-file=functions.js',`--output=${recompiled}`,compiledSource]);
 const after=inspect(recompiled).find(r=>r.record===before.record);assert.ok(after);
 for(const key of ['requests','request_phases','imports','namespace_imports','exports','indirect_exports','star_exports'])assert.deepEqual(after[key],before[key],key);
 const erased=join(dir,'erased.js'),erasedAbc=join(dir,'erased.abc');writeFileSync(erased,source.replaceAll('"use concurrent";',''));
 run(compiler,['--module','--merge-abc','--opt-level=0','--record-name=concurrent_functions',`--output=${erasedAbc}`,erased]);
 assert.equal(inspect(erasedAbc).find(r=>r.record===before.record).functions.length,0,'directive erasure mutation must change method kinds');
 const canonical=f=>({name:f.name,kind:f.kind,sendable:f.sendable,annotation_present:f.annotation_present,requests:f.requests});
 assert.deepEqual(after.functions.map(canonical).sort((a,b)=>a.name.localeCompare(b.name)),before.functions.map(canonical).sort((a,b)=>a.name.localeCompare(b.name)));
 // Independently import each recompiled method's original phase graph and
 // re-derive notification identity/order/completion. Do not require unrelated
 // root set-name stores to become decompilable on a second generation.
 assert.ok(before.functions.every(f=>f.completion_verified));
 assert.ok(after.functions.every(f=>f.completion_verified));
 writeFileSync(join(dir,'package.json'),'{"type":"module"}');copyFileSync(join(fixture,'dependency.js'),join(dir,'dependency.js'));copyFileSync(join(fixture,'extra.js'),join(dir,'extra.js')); 
 const original=join(dir,'original.mjs'),recovered=join(dir,'recovered.mjs');copyFileSync(join(fixture,'functions.js'),original);writeFileSync(recovered,source);
 const harness=`const m=await import(process.argv[2]);const trace=[];const result=[];result.push(m.explicit(3));result.push(m.implicit(trace));try{m.failed(trace)}catch(e){trace.push(e.constructor.name)};result.push(m.handled(false,trace));result.push(m.handled(true,trace));result.push(m.imported(8));result.push(m.forwarded(2));result.push(m.imported(13));console.log(JSON.stringify({result,trace}));`;
 const h=join(dir,'harness.mjs');writeFileSync(h,harness);
 const expected=run(process.execPath,[h,original]);assert.equal(run(process.execPath,[h,recovered]),expected);
 // This detects a wrong result/erased completion; directive erasure is checked
 // by recompilation metadata above because ordinary Node calls cannot see it.
 const mutated=join(dir,'mutated.mjs');writeFileSync(mutated,source.replace('return $arg0 + 1','return $arg0 + 2'));assert.notEqual(run(process.execPath,[h,mutated]),expected);
 run(process.execPath,['--check',recovered]);
 const unicode=join(dir,'unicode');run(structure,[join(fixture,'unicode.abc'),unicode,'--value-source','--language-source']);
 const unicodeRecord=JSON.parse(readFileSync(join(unicode,'manifest.json'))).records.find(r=>r.source_status!==2);
 assert.equal(unicodeRecord.source_status,3);assert.equal(unicodeRecord.source_failure.concurrent.kind,18);
 assert.equal(unicodeRecord.source_failure.pc,undefined);assert.equal(unicodeRecord.source_failure.creation_pc,undefined);
 console.log('6 concurrent functions: emitted directive; kind/sendable/ordered requests/module table round trip; completion proof on recompilation; normal/phased Node values/types/traces; result mutation detected. Taskpool/cross-thread not executed.');
} finally {rmSync(dir,{recursive:true,force:true});}
