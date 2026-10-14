import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [semantic,structure,fixture,projects]=process.argv.slice(2);
const run=(tool,args)=>{
  const p=spawnSync(tool,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});
  assert.ifError(p.error);assert.equal(p.signal,null);return p;
};
const dir=mkdtempSync(join(tmpdir(),'abcd-print-budget-'));
try {
  const exportFile=(abc,out)=>{
    const p=run(structure,[abc,out,'--value-source','--language-source']);assert.equal(p.status,0,p.stderr);
    return JSON.parse(readFileSync(join(out,'manifest.json'),'utf8'));
  };
  const output=join(dir,'fixture');const manifest=exportFile(fixture,output);
  let checked=0;
  for(const record of manifest.records.filter(r=>r.source_status===0)) {
    const metadata=JSON.parse(readFileSync(join(output,record.metadata),'utf8'));
    const entry=metadata.methods.find(m=>m.encoded_name==='func_main_0');assert.ok(entry);
    const p=run(semantic,[fixture,String(entry.entity),'--record-print-budget','--language-source']);
    assert.equal(p.status,0,p.stderr);const report=JSON.parse(p.stdout);
    assert.equal(report.count_complete,true);assert.equal(report.counted_bytes,readFileSync(join(output,record.source)).length);
    assert.ok(report.counted_peak<=report.available);++checked;
  }
  assert.ok(checked>0);
  // This Round-47 scratch refusal is now complete. The production stream
  // reserves its proven exact length, not five times an unused maximum.
  {
    const out=join(dir,'resource');const records=exportFile(projects,out);
    const record=records.records.find(r=>r.entity===7635);assert.equal(record.source_status,0);
    const metadata=JSON.parse(readFileSync(join(out,record.metadata),'utf8'));
    const entry=metadata.methods.find(m=>m.encoded_name==='func_main_0');
    const p=run(semantic,[projects,String(entry.entity),'--record-source','--language-source']);
    assert.equal(p.status,0,p.stderr);
    const text=readFileSync(join(out,record.source),'utf8');assert.equal(JSON.parse(p.stdout).source,text);
    const count=JSON.parse(run(semantic,[projects,String(entry.entity),'--record-print-budget','--language-source']).stdout);
    assert.equal(count.count_complete,true);assert.equal(count.counted_bytes,Buffer.byteLength(text));
    assert.equal(count.reservation,count.counted_bytes);
  }
  console.log(`tree_print_budget_cli_test: ${checked} exact byte counts; exact production reservation and CLI/manifest text agree`);
} finally {rmSync(dir,{recursive:true,force:true});}
