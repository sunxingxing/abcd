import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [structure,semantic,abc] = process.argv.slice(2);
const run = (exe,args,status=0) => {
  const r=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});
  assert.ifError(r.error);assert.equal(r.signal,null);assert.equal(r.status,status,r.stderr);return r.stdout;
};
const temp=mkdtempSync(join(tmpdir(),'abcd-for-in-evaluation-'));
try {
  for(const [i,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
    const out=join(temp,i);run(structure,[abc,out,'--value-source',...flags]);
    const rows=JSON.parse(readFileSync(join(out,'manifest.json'),'utf8')).records;
    for(const [record,method,pc,causes] of [
      [21089,21280,599,[['FunctionTreeError',0],['EvaluationError',2],['EvaluationObligation',0],['ForInError',8],['TreeObligation',14]]],
      [43537,44496,252,[['FunctionTreeError',0],['TreeObligation',2]]],
      [53454,54264,undefined,[['SyntaxResolutionError',1]]],
    ]) {
      const row=rows.find(r=>r.entity===record);assert.equal(row.source_complete,false);assert.equal(row.source,undefined);
      const f=row.source_failure;
      assert.deepEqual(f.causes,causes.map(([type,value])=>({domain:`abcd::source::${type}`,value})));
      assert.equal(f.method,method);assert.equal(f.pc,pc);assert.equal(f.working_pc,pc);
      const report=JSON.parse(run(semantic,[abc,String(method),'--record-source','--value-source',...flags],1));
      assert.deepEqual(report.source_failure,f);
    }
    const complete=rows.find(r=>r.entity===29268);assert.equal(complete.source_complete,true);
    const report=JSON.parse(run(semantic,[abc,'29450','--record-source','--value-source',...flags]));
    assert.equal(report.source_complete,true);assert.equal(report.source,readFileSync(join(out,complete.source),'utf8'));
    assert.equal(report.source_failure,undefined);
    // This record cannot publish the method-local for-in refusal first: an
    // earlier preparation failure in another method is still authoritative.
    const driver=rows.find(r=>r.entity===42397).source_failure;
    assert.equal(driver.method,42717);assert.equal(driver.pc,153);
    assert.deepEqual(driver.causes,[{domain:'abcd::source::PhasedMethodError',value:0},{domain:'abcd::analysis::SemanticError',value:1}]);
  }
} finally { rmSync(temp,{recursive:true,force:true}); }
console.log('for-in verified publication and next refusals: ok');
