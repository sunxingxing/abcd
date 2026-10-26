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
  for(const [label,flags] of [['default',[]],['source',['--language-source']]]) {
    const out=join(dir,label);run(structure,[input,out,'--value-source',...flags]);
    const manifest=JSON.parse(readFileSync(join(out,'manifest.json')));
    const sendable=manifest.records.find(r=>r.entity===28934),shared=manifest.records.find(r=>r.entity===24892);
      assert.equal(sendable.source_complete,true);assert.equal(sendable.source_language,'arkts');assert.match(sendable.source,/\.ets$/);
      const cli=JSON.parse(run(semantic,[input,'29066','--record-source',...flags]));
      assert.equal(cli.source_language,'arkts');assert.equal(cli.source_failure,undefined);

      assert.equal(shared.source_complete,true);assert.equal(shared.source_language,'arkts');assert.match(shared.source,/\.ets$/);
      const text=readFileSync(join(out,shared.source),'utf8');assert.match(text,/^"use shared";/);
      const sharedCli=JSON.parse(run(semantic,[input,'25192','--record-source',...flags]));assert.equal(sharedCli.source_language,'arkts');assert.equal(sharedCli.source_failure,undefined);

  }
  console.log('metadata scale 200/201/400/800 + exact reader budgets; default/alias Sendable and shared publication verified; CLI/manifest agree');
} finally { rmSync(dir,{recursive:true,force:true}); }
