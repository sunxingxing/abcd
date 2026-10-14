import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,copyFileSync,rmSync} from 'node:fs';
import {join,dirname} from 'node:path';
import {tmpdir} from 'node:os';
const [structure,semantic,fixtures]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-logical-coverage-'));
try {
 const oracle=join(dir,'oracle.mjs');copyFileSync(join(fixtures,'coverage.js'),oracle);
 const runner=join(dir,'runner.mjs');
 writeFileSync(runner,`
 import {pathToFileURL} from 'node:url';
 const m=await import(pathToFileURL(process.argv[2])), results=[];
 for(const name of ['retained','short','nested','argument'])
 for(const mode of ['yesPath','noPath','innerNo','truth','test','other','yes','no','fallback','before','later','call','invoke','rewrite']) {
   const trace=[];let generation=0;
   const get=(key,value)=>{trace.push(key);if(mode===key)throw new RangeError(key);return value;};
   const truth={valueOf(){trace.push('valueOf');throw new Error('not ToBoolean');}};
   const o={
     get test(){return get('test',mode==='noPath'||mode==='no'||mode==='fallback'?false:mode==='truth'?truth:true)},
     get other(){return get('other',mode!=='innerNo')},
     get yes(){if(mode==='rewrite')++generation;return get('yes',11)},
     get no(){return get('no',23)},get fallback(){return get('fallback',37)},
     get before(){return get('before',41)},get later(){return get('later',43)},
     get call(){get('call');const saved=generation;return function(value){
       if(this!==o)throw new Error('receiver');trace.push(['invoke',saved]);
       if(mode==='invoke')throw new SyntaxError();return value;};}
   };
   let result;try{result=m[name](o);}catch(e){result={error:e.constructor.name};}
   results.push({name,mode,result,trace});
 }
 console.log(JSON.stringify(results));
 `);
 const run=file=>{const p=spawnSync(process.execPath,[runner,file],{encoding:'utf8',timeout:10000});assert.equal(p.status,0,p.stderr);return JSON.parse(p.stdout);};
 const expected=run(oracle);assert.ok(expected.every(r=>!r.trace.includes('valueOf')));
 let comparisons=0,mutants=0;
 for(const [label,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
   const out=join(dir,label),abc=join(fixtures,'coverage.abc');
   const p=spawnSync(structure,[abc,out,'--value-source',...flags],{encoding:'utf8',timeout:30000});assert.equal(p.status,0,p.stderr);
   const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
   assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
   const file=join(out,records[0].source),text=readFileSync(file,'utf8');
   const metadata=JSON.parse(readFileSync(join(out,records[0].metadata)));
   for(const name of ['retained','short','nested','argument']) {
     const method=metadata.methods.find(m=>m.runtime_name===name);assert.ok(method);
     const dis=spawnSync(semantic,[abc,String(method.entity),'--instructions'],{encoding:'utf8'});assert.equal(dis.status,0,dis.stderr);
     const ops=dis.stdout.trim().split('\n').map(JSON.parse);
     const branch=ops.find(i=>i.opcode===0x4f||i.opcode===0x51);
     const allocation=ops.find(i=>i.opcode===0x07),definition=ops.find(i=>i.opcode===0xdc);
     assert.ok(branch&&allocation&&definition);assert.ok(branch.pc<allocation.pc&&allocation.pc<definition.pc);
   }
   assert.deepEqual(run(file),expected,label);comparisons+=expected.length;
   // No control/layout assertion: these mutations independently distinguish
   // condition identity, selected value, read count/order and late member lookup.
   const changed=[
     text.replaceAll('$arg0.test','!$arg0.test'),
     text.replaceAll('$arg0.yes','($arg0.yes, $arg0.yes)'),
     text.replaceAll('$arg0.no','$arg0.yes'),
     text.replaceAll('value: $phi1','value: null'),
     text.replaceAll('$arg0.before','($arg0.later, $arg0.before)'),
   ];
   for(const [i,mutant] of changed.entries()) {
     assert.notEqual(mutant,text);const path=join(dirname(file),'mutant'+i+'.mjs');writeFileSync(path,mutant);
     assert.notDeepEqual(run(path),expected,'undetected mutant '+i);++mutants;
   }
 }
 console.log('logical_coverage_cli_test:',comparisons,'differentials,',mutants,'detected mutants');
} finally {rmSync(dir,{recursive:true,force:true});}
