import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,copyFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {createHash} from 'node:crypto';
const [structure,probe,fixture]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-call-spread-'));
const run=(exe,args)=>{const r=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});assert.equal(r.status,0,r.stderr);return r.stdout;};
try {
 const metadata=JSON.parse(readFileSync(join(fixture,'manifest.json')));
 for(const [file,hash] of Object.entries(metadata.hashes)) assert.equal(createHash('sha256').update(readFileSync(join(fixture,file))).digest('hex'),hash);
 const rows=run(probe,[join(fixture,'calls.abc')]).trim().split('\n').map(JSON.parse).filter(x=>x.type==='instruction');
 for(const opcode of metadata.opcodes) assert.ok(rows.some(x=>x.encoding.opcode===opcode));
 for(const row of rows.filter(x=>x.encoding.opcode===186)) {assert.equal(row.lift.status,'ok');assert.equal(row.lift.roles.form,1);}
 const out=join(dir,'out');run(structure,[join(fixture,'calls.abc'),out,'--value-source','--language-source']);
 const manifest=JSON.parse(readFileSync(join(out,'manifest.json')));assert.equal(manifest.source_contract,'language_source_v6');
 const record=manifest.records.find(r=>r.entity===metadata.record);assert.equal(record.source_status,0,JSON.stringify(record));
 const emitted=join(out,record.source);run(process.execPath,['--check',emitted]);
 const oracle=join(dir,'oracle.mjs');copyFileSync(join(fixture,'calls.js'),oracle);
 const runner=join(dir,'runner.mjs');
 writeFileSync(runner,`
 const m=await import(process.argv[2]); const mode=process.argv[3]; const log=[];
 const receiver={tag:42}; Object.defineProperty(receiver,'method',{configurable:true,get(){log.push('callee'); if(mode==='callee')throw new RangeError(); if(mode==='notCallable')return 7; return function(...args){log.push(['call',this===receiver,args]);return args;};}});
 const values={ [Symbol.iterator](){log.push('iterator');if(mode==='iterator')throw new TypeError();let i=0;return {next(){log.push('next');if(mode==='next')throw new SyntaxError();const n=i++;return {get done(){log.push('done');if(mode==='done')throw new ReferenceError();return n===2;},get value(){log.push('value');if(mode==='value')throw new EvalError();return n+10;}};}};}};
 const input={get first(){log.push('first');if(mode==='first')throw new RangeError();return 1;},get values(){log.push('values');return values;},get last(){log.push('last');if(mode==='rewrite')Object.defineProperty(receiver,'method',{value(){throw new Error('reread');},configurable:true});if(mode==='last')throw new URIError();return 2;}};
 let result,error;try {result=m.mixed(receiver,input);}catch(e){error=e.constructor.name;}
 const samples=[[],[1,,3],'a😀'];const ordinary=samples.map(v=>m.direct(function(...xs){return [this===undefined,xs];},v));
 const repeated=m.twice({method(...xs){return xs;}},[1],[2,3]);
 const caught=m.handled({method(){throw new RangeError();}},[]);
 const nestedLog=[];const array=[1,2,3];
 Object.defineProperty(array,'indexOf',{get(){nestedLog.push('indexOf.get');return function(x){nestedLog.push(['indexOf.call',this===array,x]);return Array.prototype.indexOf.call(this,x);};}});
 const xs={[Symbol.iterator](){nestedLog.push('iterator');return [1,2][Symbol.iterator]();}};
 const inner=function(...args){nestedLog.push(['inner',this===undefined,args]);if(mode==='childThrow')throw new TypeError();return 2;};
 let indexed,argument,nestedError;try{indexed=m.indexNested(array,inner,xs);argument=m.argumentNested(function(x,y){nestedLog.push(['outer',this===undefined,x,y]);return x+y;},inner,xs,4);}catch(e){nestedError=e.constructor.name;}
 console.log(JSON.stringify({result,error,log,ordinary,repeated,caught,indexed,argument,nestedError,nestedLog}));
 `);
 for(const mode of ['normal','callee','first','iterator','next','done','value','last','rewrite','notCallable','childThrow'])
   assert.equal(run(process.execPath,[runner,emitted,mode]),run(process.execPath,[runner,oracle,mode]),mode);
 // The second acquisition is an accepted difference, not an Ark oracle pass.
 const difference=run(process.execPath,['--input-type=module','-e',`
 const source={[Symbol.iterator](){let done=false;return {next(){if(done)return {done:true};done=true;return {done:false,value:7};}};}};
 const descriptor=Object.getOwnPropertyDescriptor(Array.prototype,Symbol.iterator);let direct,extra;
 Object.defineProperty(Array.prototype,Symbol.iterator,{configurable:true,get(){throw new RangeError();}});
 try {direct=((x)=>x)(...source);try{((x)=>x)(...[...source]);}catch(e){extra=e.constructor.name;}}
 finally{Object.defineProperty(Array.prototype,Symbol.iterator,descriptor);}
 console.log(JSON.stringify({direct,extra}));`]);
 assert.deepEqual(JSON.parse(difference),{direct:7,extra:'RangeError'});
 // Recompile the actual engine output, not the oracle source.
 assert.equal(createHash('sha256').update(readFileSync(metadata.compiler)).digest('hex'),metadata.compiler_sha256);
 // Pinned es2abc predates string-named export syntax. Adapt only identifier exports.
 const recompSource=join(dir,'calls.js');writeFileSync(recompSource,readFileSync(emitted,'utf8').replace(/ as "([A-Za-z_$][A-Za-z0-9_$]*)"/g,' as $1'));
 const recompAbc=join(dir,'roundtrip.abc');
 run(metadata.compiler,[...metadata.options,'--output='+recompAbc,recompSource]);
 const reRows=run(probe,[recompAbc]).trim().split('\n').map(JSON.parse).filter(x=>x.type==='instruction');
 const structures=rs=>{const methods=new Map();for(const r of rs){const id=r.method;const a=methods.get(id)||[];if(metadata.opcodes.includes(r.encoding.opcode))a.push(r.encoding.opcode);methods.set(id,a);}return [...methods.values()].filter(a=>a.includes(186)).map(a=>JSON.stringify(a)).sort();};
 assert.deepEqual(structures(reRows),structures(rows),'per-method createarray/spread/APPLY round-trip');
 for(const r of reRows.filter(x=>x.encoding.opcode===186))assert.equal(r.lift.roles.form,1);
 // Nested argument-array constructions retain their separate shared-operand gate.
 const nestedOut=join(dir,'nested-refused');run(structure,[join(fixture,'nested_refused.abc'),nestedOut,'--value-source','--language-source']);
 const nestedRecords=JSON.parse(readFileSync(join(nestedOut,'manifest.json'))).records;
 assert.ok(nestedRecords.some(r=>r.source_failure?.causes?.some(c=>c.domain==='abcd::source::ArrayInitializationError'&&c.value===11)));
 const old=join(dir,'old');run(structure,[join(fixture,'calls.abc'),old,'--value-source','--language-numeric','--language-constructors']);
 const refused=JSON.parse(readFileSync(join(old,'manifest.json'))).records.find(r=>r.entity===metadata.record);
 assert.notEqual(refused.source_status,0);assert.ok(refused.source_failure.causes.some(c=>c.domain==='abcd::source::CallSpreadError'&&c.value===9));
} finally {rmSync(dir,{recursive:true,force:true});}
