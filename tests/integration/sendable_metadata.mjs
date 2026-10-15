// Metadata/diagnostic consistency: old contracts refuse; V3 publishes verified classes/shared modules.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,writeFileSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure,semantic,unit,compiler,input,probe]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-sendable-metadata-'));
const run=(exe,args,status=0)=>{const r=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:8*1024*1024});assert.ifError(r.error);assert.equal(r.status,status,r.stderr);return r.stdout;};
try {
  const a=JSON.parse(run(probe,[input,'29066','9']));
  assert.deepEqual(a.fields.map(f=>[f.name,f.mask,f.placement]),[['taskId',1,0]]);
  const b=JSON.parse(run(probe,[input,'25192','12']));
  assert.deepEqual(b.fields.map(f=>[f.name,f.mask,f.placement]),[['lock_',32,0],['books_',32,0]]);
  assert.equal(b.constructor,25220);assert.equal(b.literal,25573);assert.equal(b.field_types,25544);
  for(const n of [200,201,400,800]) {
    const ts=join(dir,`scale${n}.ts`),abc=join(dir,`scale${n}.abc`);
    writeFileSync(ts,`export class Fields {\n${Array.from({length:n},(_,i)=>`f${i}: number;`).join('\n')}\nconstructor() { "use sendable"; }\n}`);
    run(compiler,['--module','--merge-abc','--extension=ts','--opt-level=0',`--output=${abc}`,ts]);
    assert.match(run(unit,[abc]),new RegExp(`fields=${n} instance=${n}`));
  }
  for(const [label,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
    const out=join(dir,label);run(structure,[input,out,'--value-source',...flags]);
    const manifest=JSON.parse(readFileSync(join(out,'manifest.json')));
    const sendable=manifest.records.find(r=>r.entity===28934),shared=manifest.records.find(r=>r.entity===24892);
    if(label==='source') {
      assert.equal(sendable.source_complete,true);assert.equal(sendable.source_language,'arkts');assert.match(sendable.source,/\.ets$/);
      const cli=JSON.parse(run(semantic,[input,'29066','--record-source',...flags]));
      assert.equal(cli.source_language,'arkts');assert.equal(cli.source_failure,undefined);
    } else {
    assert.equal(sendable.source_status,3);assert.equal(sendable.source,undefined);
    assert.deepEqual(sendable.source_failure.causes,[{domain:'abcd::analysis::SemanticError',value:1}]);
    const s=sendable.source_failure.sendable;
    assert.equal(s.kind,0);assert.equal(s.record,28934);assert.equal(s.method,29066);
    assert.equal(s.pc,9);assert.equal(s.working_pc,9);assert.equal(s.constructor,29121);
    assert.equal(s.literal,29308);assert.equal(s.field_types,29289);assert.equal(s.pool,497);
    assert.equal(s.field_count,1);assert.equal(s.instance_count,1);assert.equal(s.member_count,1);
    const cli=JSON.parse(run(semantic,[input,'29066','--record-source',...flags],1));
    assert.deepEqual(cli.source_failure,sendable.source_failure);
    }
    if(label==='source') {
      assert.equal(shared.source_complete,true);assert.equal(shared.source_language,'arkts');assert.match(shared.source,/\.ets$/);
      const text=readFileSync(join(out,shared.source),'utf8');assert.match(text,/^"use shared";/);
      const cli=JSON.parse(run(semantic,[input,'25192','--record-source',...flags]));assert.equal(cli.source_language,'arkts');assert.equal(cli.source_failure,undefined);
    } else {
    assert.equal(shared.source_status,3);assert.equal(shared.source,undefined);
    assert.equal(shared.source_error,2);assert.deepEqual(shared.source_causes,[{domain:'abcd::source::RecordModuleKindError',value:1}]);
    const m=shared.source_shared_module;
    assert.equal(m.kind,0);assert.equal(m.record,24892);assert.equal(m.shared,true);assert.equal(m.module,25407);
    assert.equal(m.requests,1);assert.equal(m.imports,1);assert.equal(m.local_exports,1);
    assert.equal(m.namespace_imports,0);assert.equal(m.indirect_exports,0);assert.equal(m.star_exports,0);
    const sharedCli=JSON.parse(run(semantic,[input,'25192','--record-source',...flags],1));
    assert.deepEqual(sharedCli.source_shared_module,m);
    }
  }
  console.log('metadata scale 200/201/400/800 + exact reader budgets; old Sendable gate preserved; V3 Sendable publishes; old shared gate preserved; V3 shared publishes; CLI/manifest agree');
} finally { rmSync(dir,{recursive:true,force:true}); }
