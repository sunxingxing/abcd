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
  // A resource failure exercises the real record-entry serialization and the
  // abc_semantic boundary, rather than inventing a charge at that boundary.
  {
    const out=join(dir,'resource');const records=exportFile(projects,out);
    const record=records.records.find(r=>r.entity===7635);assert.ok(record.source_printing?.budget);
    const metadata=JSON.parse(readFileSync(join(out,record.metadata),'utf8'));
    const entry=metadata.methods.find(m=>m.encoded_name==='func_main_0');
    const p=run(semantic,[projects,String(entry.entity),'--record-source','--language-source']);
    assert.equal(p.status,1);assert.deepEqual(JSON.parse(p.stdout).source_printing,record.source_printing);
    const count=JSON.parse(run(semantic,[projects,String(entry.entity),'--record-print-budget','--language-source']).stdout);
    assert.equal(count.count_complete,true);assert.deepEqual(count.source_printing,record.source_printing);
    assert.ok(count.counted_bytes<count.reservation);
  }
  console.log(`tree_print_budget_cli_test: ${checked} exact byte counts; resource CLI/manifest evidence agrees`);
} finally {rmSync(dir,{recursive:true,force:true});}
