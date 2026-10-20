import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {createHash} from 'node:crypto';
const [engine,fixture]=process.argv.slice(2);
const temp=mkdtempSync(join(tmpdir(),'abcd-logical-references-'));
const run=(exe,args)=>{const p=spawnSync(exe,args,{encoding:'utf8'});assert.equal(p.status,0,p.stderr);return p.stdout;};
try {
  const metadata=JSON.parse(readFileSync(join(fixture,'manifest.json')));
  for(const [file,sha]of Object.entries(metadata.hashes))assert.equal(createHash('sha256').update(readFileSync(join(fixture,file))).digest('hex'),sha);
  const original=join(temp,'original.mjs');writeFileSync(original,readFileSync(join(fixture,'calls.js')));
  const runner=join(temp,'run.mjs');
  writeFileSync(runner,`
const m=await import(process.argv[2]);const trace=[];let count=0;
for(const [name,length]of [['optional',3],['conditional',2],['earlier',4]])if(m[name].length!==length||m[name].name!==name)throw new Error('function ABI');
for(const name of ['optional','conditional','earlier'])
for(const first of [null,undefined,0,false,1,'value'])for(const second of [null,undefined,0,false,1,'value'])
for(const mode of ['normal','callee-throw','noncallable','prefix-throw','middle-throw','read-throw','before-throw','after-throw','left-throw','right-throw','replace','coercion-throw','yes-throw','no-throw','first-throw','second-throw']) {
  trace.push([name,first,second,mode]);let reads=0;const receiver={};
  const replace=()=>Object.defineProperty(receiver,'invoke',{configurable:true,value(){trace.push('replacement');return 'wrong';}});
  Object.defineProperty(receiver,'invoke',{configurable:true,get(){trace.push('callee');if(mode==='callee-throw')throw new RangeError();
    if(mode==='noncallable')return 17;
    return function(...args){trace.push(['invoke',this===receiver,args]);return args;};}});
  const value=(key,v)=>{trace.push(key);if(mode===key+'-throw')throw new SyntaxError();if(mode==='replace')replace();return v;};
  const input={get prefix(){return value('prefix','from:');},get middle(){return value('middle','to:');},
    get first(){return value('first',first);},get second(){return value('second',second);},
    get yes(){return value('yes','yes');},get no(){return value('no','no');}};
  const monitor={get read(){trace.push('read getter');return function(){trace.push(['read',this===monitor,reads]);
    if(mode==='read-throw')throw new EvalError();if(mode==='replace')replace();const v=reads++===0?first:second;
    if(v===null||v===undefined)return v;
    return {get before(){return value('before',mode==='coercion-throw'?{[Symbol.toPrimitive](){trace.push('coercion');throw new URIError();}}:v);},
      get after(){return value('after',v);}};};}};
  const left={get value(){return value('left',first);}},right={get value(){return value('right',second);}};
  try {trace.push(['result',name==='optional'?m[name](receiver,monitor,input):name==='conditional'?m[name](receiver,input):m[name](receiver,left,right,monitor)]);}
  catch(e){trace.push(['error',e.constructor.name]);}
  ++count;
}
console.log(JSON.stringify({count,trace}));`);
  const expected=run(process.execPath,[runner,original]);assert.equal(JSON.parse(expected).count,1728);
  for(const abc of ['calls.abc','default.abc']) {
    const output=join(temp,abc);run(engine,[join(fixture,abc),output,'--value-source','--language-source']);
    const record=JSON.parse(readFileSync(join(output,'manifest.json'))).records.find(r=>r.source_status===0);
    assert.ok(record,abc);const source=join(output,record.source);run(process.execPath,['--check',source]);
    assert.equal(run(process.execPath,[runner,source]),expected,abc);
    const text=readFileSync(source,'utf8');assert.match(text,/\?\./);assert.ok(!text.includes('.call(')&&!text.includes('.apply('));
    // An ordinary value-call replacement must be observable: native member
    // reference identity is still required independently of argument ownership.
    const wrong=join(temp,abc+'-wrong.mjs');assert.ok(/\$arg0\.invoke\(/.test(text));
    writeFileSync(wrong,text.replaceAll('$arg0.invoke(','(0, $arg0.invoke)('));
    assert.notEqual(run(process.execPath,[runner,wrong]),expected);
  }
  // Retained phi predicate/keep-edge consumption has not been widened.
  const blocked=join(temp,'retained');run(engine,[join(fixture,'retained.abc'),blocked,'--value-source','--language-source']);
  const refusal=JSON.parse(readFileSync(join(blocked,'manifest.json'))).records.find(r=>r.source_status===3);
  assert.ok(refusal?.source_failure?.call_reference);
  assert.equal(refusal.source_failure.call_reference.kind,1);
  console.log('3456 engine-output/original scenarios passed (O0/default), receiver mutation observed; retained joint-use refusal kept');
} finally {rmSync(temp,{recursive:true,force:true});}
