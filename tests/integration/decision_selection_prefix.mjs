import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,copyFileSync,rmSync} from 'node:fs';
import {join,dirname} from 'node:path';
import {tmpdir} from 'node:os';
const [structure,semantic,fixtures]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-decision-selection-prefix-'));
try {
 const oracle=join(dir,'oracle.mjs');copyFileSync(join(fixtures,'prefix.js'),oracle);
 const runner=join(dir,'runner.mjs');
 writeFileSync(runner,`
 import {pathToFileURL} from 'node:url';
 const m=await import(pathToFileURL(process.argv[2])), results=[];
 for(const name of ['check','andCheck','nested','staged'])
 for(const first of [false,true,'object']) for(const flag of [false,true]) for(const inner of [false,true])
 for(const mode of ['normal','mismatch','before','first','flag','inner','left','middle','right','test','yes','no','later','rewrite']) {
   const trace=[];let changed=false;
   const get=(key,value)=>{trace.push(key);if(mode===key)throw new RangeError(key);return value;};
   const truth={valueOf(){trace.push('valueOf');throw new Error('ToBoolean must not coerce');}};
   const o={get first(){return get('first',first==='object'?truth:first)},
     get flag(){return get('flag',flag)},get inner(){return get('inner',inner)},
     get left(){if(mode==='rewrite')changed=true;return get('left',11)},
     get middle(){return get('middle',23)},get right(){return get('right',37)},
     get test(){return get('test',mode==='mismatch'?99:changed?71:flag?(name==='nested'&&!inner?23:11):37)},
     before(){if(this!==o)throw new Error('receiver');get('before',0)},
     yes(){if(this!==o)throw new Error('receiver');get('yes',0)},
     no(){if(this!==o)throw new Error('receiver');get('no',0)},get later(){return get('later',43)}};
   let result;try{result=m[name](o);}catch(e){result={error:e.constructor.name};}
   results.push({name,first,flag,inner,mode,result,trace});
 }
 console.log(JSON.stringify(results));
 `);
 const run=file=>{const p=spawnSync(process.execPath,[runner,file],{encoding:'utf8',timeout:10000,maxBuffer:8*1024*1024});assert.equal(p.status,0,p.stderr);return JSON.parse(p.stdout);};
 const expected=run(oracle);assert.ok(expected.every(r=>!r.trace.includes('valueOf')));
 let comparisons=0,mutants=0;
 for(const [label,flags] of [['default',[]],['source',['--language-source']]]) {
   const out=join(dir,label),abc=join(fixtures,'prefix.abc');
   const p=spawnSync(structure,[abc,out,'--value-source',...flags],{encoding:'utf8',timeout:30000});assert.equal(p.status,0,p.stderr);
   const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
   assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
   const file=join(out,records[0].source),text=readFileSync(file,'utf8');
   const metadata=JSON.parse(readFileSync(join(out,records[0].metadata)));
   for(const name of ['check','andCheck','nested','staged']) {
     const method=metadata.methods.find(m=>m.runtime_name===name);assert.ok(method);
     const dis=spawnSync(semantic,[abc,String(method.entity),'--instructions'],{encoding:'utf8'});assert.equal(dis.status,0,dis.stderr);
     const ops=dis.stdout.trim().split('\n').map(JSON.parse);
     assert.ok(ops.filter(i=>i.opcode===0x4f||i.opcode===0x51).length>=3);
     assert.ok(ops.some(i=>i.opcode===0x42)); // named property access
   }
   assert.deepEqual(run(file),expected,label);comparisons+=expected.length;
   const changed=[text.replaceAll('$arg0.flag','!$arg0.flag'),
     text.replaceAll('$arg0.left','($arg0.left, $arg0.left)'),
     text.replaceAll('$arg0.right','$arg0.left'),
     text.replaceAll(' || ',' && '),
     text.replaceAll('$arg0.test','($arg0.later, $arg0.test)')];
   for(const [i,mutant] of changed.entries()) {
     assert.notEqual(mutant,text);const path=join(dirname(file),'mutant'+i+'.mjs');writeFileSync(path,mutant);
     assert.notDeepEqual(run(path),expected,'undetected mutant '+i);++mutants;
   }
 }
 console.log('decision_selection_prefix_cli_test:',comparisons,'differentials,',mutants,'detected mutants');
} finally {rmSync(dir,{recursive:true,force:true});}
