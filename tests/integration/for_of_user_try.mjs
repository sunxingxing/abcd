import {recordNodeEvidence} from '../support/isa/oracle_evidence.mjs';
// ABC-only production input. Independent sources are evaluated only by Node.
import assert from 'node:assert/strict';
import {traceAsyncForOf as runAsync} from '../support/for_of_async_oracle.mjs';
import {execFileSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
const [engine,fixtures]=process.argv.slice(2);
const root=mkdtempSync(join(tmpdir(),'abcd-for-of-user-try-'));
try {
  const out=join(root,'main');
  execFileSync(engine,[join(fixtures,'main.abc'),out,'--value-source','--language-source']);
  const record=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.source_status!==2);
  assert.equal(record.source_status,0,JSON.stringify(record.source_failure));
  const file=join(out,record.source);execFileSync(process.execPath,['--check',file]);
  const actual=(await import(pathToFileURL(file))).scan;
  const expected=(await import('data:text/javascript;base64,'+Buffer.from(readFileSync(join(fixtures,'main.js'))).toString('base64'))).scan;
  function run(fn,values,mode,outcome,throwAt,member=false,rhsThrow=false) {
    const log=[],body=new RangeError('body'),close=new URIError('close');
    const saved=globalThis.bodyError;globalThis.bodyError=body;
    const input={[Symbol.iterator](){
      log.push('acquire');let i=0;
      const it={get next(){log.push('next-get');return function(){log.push(['next',this===it]);return i<values.length?{done:false,value:values[i++]}:{done:true};}}};
      if(outcome!=='missing')Object.defineProperty(it,'return',{get(){
        log.push('return-get');if(outcome==='getter-throw')throw close;if(outcome==='null')return null;
        return function(){log.push(['return',this===it]);if(outcome==='call-throw')throw close;return outcome==='primitive'?1:{};};
      }});return it;
    }};
    let calls=0;
    try {
      const argument=member?{get items(){log.push('rhs-get');if(rhsThrow)throw close;return input;}}:input;
      const value=fn(argument,x=>{log.push(x instanceof Error?['catch',x.constructor.name,x===body,x===close]:['visit',x]);if(!(x instanceof Error)&&++calls===throwAt)throw body;},mode);
      log.push(['result',value]);
    }catch(e){log.push(['escaped',e.constructor.name,e===body,e===close]);}
    finally{globalThis.bodyError=saved;}
    return log;
  }
  let cases=0;
  for(const values of [[],[1],[1,2]])for(const mode of ['normal','break','return','throw'])
    for(const outcome of ['object','missing','null','getter-throw','call-throw','primitive'])for(const at of [0,1,2]) {
      assert.deepEqual(run(actual,values,mode,outcome,at),run(expected,values,mode,outcome,at),`${values}/${mode}/${outcome}/${at}`);++cases;
    }
  const memberOut=join(root,'member');
  execFileSync(engine,[join(fixtures,'member.abc'),memberOut,'--value-source','--language-source']);
  const memberRecord=JSON.parse(readFileSync(join(memberOut,'manifest.json'))).records.find(r=>r.source_status!==2);
  assert.equal(memberRecord.source_status,0,JSON.stringify(memberRecord.source_failure));
  const memberFile=join(memberOut,memberRecord.source);execFileSync(process.execPath,['--check',memberFile]);
  const memberActual=(await import(pathToFileURL(memberFile))).scan;
  const memberExpected=(await import('data:text/javascript;base64,'+Buffer.from(readFileSync(join(fixtures,'member.js'))).toString('base64'))).scan;
  for(const values of [[],[1],[1,2]])for(const mode of ['normal','break','return','throw'])
    for(const outcome of ['object','missing','null','getter-throw','call-throw','primitive'])for(const at of [0,1,2]) {
      assert.deepEqual(run(memberActual,values,mode,outcome,at,true),run(memberExpected,values,mode,outcome,at,true),`member/${values}/${mode}/${outcome}/${at}`);++cases;
    }
  assert.deepEqual(run(memberActual,[1],'break','object',0,true,true),run(memberExpected,[1],'break','object',0,true,true));++cases;
  const asyncOut=join(root,'async');
  execFileSync(engine,[join(fixtures,'async.abc'),asyncOut,'--value-source','--language-source']);
  const asyncRecord=JSON.parse(readFileSync(join(asyncOut,'manifest.json'))).records.find(r=>r.source_status!==2);
  assert.equal(asyncRecord.source_status,0,JSON.stringify(asyncRecord.source_failure));
  const asyncFile=join(asyncOut,asyncRecord.source);execFileSync(process.execPath,['--check',asyncFile]);
  const asyncActual=(await import(pathToFileURL(asyncFile))).scan;
  const asyncExpected=(await import('data:text/javascript;base64,'+Buffer.from(readFileSync(join(fixtures,'async.js'))).toString('base64'))).scan;
  for(const values of [[],[1],[1,2]])for(const mode of ['normal','break','return','throw'])
    for(const outcome of ['object','missing','null','getter-throw','call-throw','primitive'])for(const at of [0,1,2])for(const rejected of [false,true]) {
      assert.deepEqual(await runAsync(asyncActual,values,mode,outcome,at,rejected),await runAsync(asyncExpected,values,mode,outcome,at,rejected),`async/${values}/${mode}/${outcome}/${at}/${rejected}`);++cases;
    }
  // Finally completion propagation keeps its separately typed gate.
  const refused={};
  for(const name of ['finally']) {
    const dest=join(root,name);execFileSync(engine,[join(fixtures,name+'.abc'),dest,'--value-source','--language-source']);
    const r=JSON.parse(readFileSync(join(dest,'manifest.json'))).records.find(r=>r.source_status!==2);
    assert.equal(r.source_status,3,name);assert.ok(r.source_failure.causes.some(c=>c.domain==='abcd::source::ExceptionControlError'&&c.value===4),name);
    refused[name]=r.source_failure;
  }
  recordNodeEvidence({abc:join(fixtures,'main.abc'),output:out,original:join(fixtures,'main.js'),producer:import.meta.url,cases:216,engine});
  recordNodeEvidence({abc:join(fixtures,'member.abc'),output:memberOut,original:join(fixtures,'member.js'),producer:import.meta.url,cases:217,engine});
  recordNodeEvidence({abc:join(fixtures,'async.abc'),output:asyncOut,original:join(fixtures,'async.js'),producer:import.meta.url,cases:432,engine});
  console.log(JSON.stringify({published:3,differential_cases:cases,refused}));
}finally{rmSync(root,{recursive:true,force:true});}
