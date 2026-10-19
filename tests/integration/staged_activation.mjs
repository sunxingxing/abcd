import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {createHash} from 'node:crypto';
const [engine,fixture]=process.argv.slice(2);
const temp=mkdtempSync(join(tmpdir(),'abcd-staged-activation-'));
const run=(command,args)=>{const p=spawnSync(command,args,{encoding:'utf8'});assert.equal(p.status,0,p.stderr);return p.stdout;};
try {
 const metadata=JSON.parse(readFileSync(join(fixture,'manifest.json')));
 for(const [file,hash] of Object.entries(metadata.hashes))assert.equal(createHash('sha256').update(readFileSync(join(fixture,file))).digest('hex'),hash);
 const output=join(temp,'output');run(engine,[join(fixture,'loops.abc'),output,'--value-source','--language-source']);
 const record=JSON.parse(readFileSync(join(output,'manifest.json'))).records.find(r=>r.source_status===0);
 assert.ok(record);const emitted=join(output,record.source);run(process.execPath,['--check',emitted]);
 const original=join(temp,'original.mjs');writeFileSync(original,readFileSync(join(fixture,'loops.js')));
 const runner=join(temp,'run.mjs');writeFileSync(runner,`
 const m=await import(process.argv[2]);const mode=process.argv[3];const log=[];const identities=new Set();
 const read=(i,j)=>{log.push(['read',i,j]);if(mode==='throw'&&i===1)throw new RangeError();return i%2;};
 const choose=i=>{log.push(['choose',i]);return i%2;};
 const consume=o=>{log.push(['value',o.value,o.index,identities.has(o),Object.keys(o)]);identities.add(o);};
 for(const n of [0,1,3])for(const name of ['conditional','surrounding','nested']){
 log.push([name,n]);try{if(name==='conditional')m[name](n,choose,read,consume);else m[name](n,read,consume);}catch(e){log.push(['throw',e.constructor.name]);}}
 console.log(JSON.stringify(log));`);
 for(const mode of ['normal','throw'])assert.equal(run(process.execPath,[runner,emitted,mode]),run(process.execPath,[runner,original,mode]));
 // A catch cycle within the unfinished activation stays refused; no dropped
 // exception edge proves completion.
 const caught=join(temp,'caught');run(engine,[join(fixture,'caught.abc'),caught,'--value-source','--language-source']);
 const failure=JSON.parse(readFileSync(join(caught,'manifest.json'))).records.find(r=>r.source_failure)?.source_failure;
 assert.ok(failure?.causes.some(c=>c.domain==='abcd::source::EvaluationInputCheck'&&c.value===9));
} finally {rmSync(temp,{recursive:true,force:true});}
