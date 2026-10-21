import {recordNodeEvidence} from '../support/isa/oracle_evidence.mjs';
// Independent Node oracle; only ABC goes into the production engine.
import assert from 'node:assert/strict';
import {traceAsyncMemberForOf as run} from '../support/for_of_async_oracle.mjs';
import {execFileSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
const [engine,fixtures]=process.argv.slice(2);
const root=mkdtempSync(join(tmpdir(),'abcd-for-of-composition-'));
try {
  async function load(name,exported) {
    const dest=join(root,name);execFileSync(engine,[join(fixtures,name+'.abc'),dest,'--value-source','--language-source']);
    const record=JSON.parse(readFileSync(join(dest,'manifest.json'))).records.find(r=>r.source_status!==2);
    assert.equal(record.source_status,0,JSON.stringify(record.source_failure));
    const file=join(dest,record.source);execFileSync(process.execPath,['--check',file]);
    return [(await import(pathToFileURL(file)))[exported],(await import('data:text/javascript;base64,'+Buffer.from(readFileSync(join(fixtures,name+'.js'))).toString('base64')))[exported]];
  }
  const [actual,expected]=await load('async','collect');
  let cases=0;
  for(const count of [0,1,3])for(const fail of ['none','getter','throw','reject'])for(const close of ['object','getter','call','null','primitive']) {
    assert.deepEqual(await run(actual,count,fail,close),await run(expected,count,fail,close),`${count}/${fail}/${close}`);++cases;
  }
  recordNodeEvidence({abc:join(fixtures,'async.abc'),output:join(root,'async'),original:join(fixtures,'async.js'),producer:import.meta.url,cases:60,engine});
  console.log(JSON.stringify({published:1,differential_cases:cases}));
}finally{rmSync(root,{recursive:true,force:true});}
