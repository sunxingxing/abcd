import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,copyFileSync,rmSync} from 'node:fs';
import {join,dirname} from 'node:path';
import {tmpdir} from 'node:os';
const [structure,semantic,fixtures]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-snapshot-candidates-'));
const values=`export let state; export function setState(value) { state=value; }\n`;
try {
 writeFileSync(join(dir,'package.json'),' {"type":"module"} ');
 writeFileSync(join(dir,'values.js'),values);
 const oracle=join(dir,'oracle.mjs');copyFileSync(join(fixtures,'candidates.js'),oracle);
 const runner=join(dir,'run.mjs');
 writeFileSync(runner,`
 import {pathToFileURL} from 'node:url';
 const file=pathToFileURL(process.argv[2]);
 const m=await import(file.href), {setState}=await import(new URL('./values.js',file));
 const mode=process.argv[3], name=process.argv[4];const trace=[];let gets=0;
 const x={valueOf(){trace.push('valueOf');throw new Error();}};
 const second={get B(){trace.push('new.B');if(mode==='new.B')throw new URIError();return x;}};
 const original={get A(){trace.push('A');if(mode==='A')throw new RangeError();if(mode==='rebind'||mode==='new.B')setState(second);return mode==='first'?x:null;},get B(){trace.push('B');if(mode==='B')throw new SyntaxError();return mode==='none'?null:x;}};
 setState(original);
 const list={get 0(){trace.push('index'+(++gets));if(mode==='index')throw new TypeError();return gets===1?x:null;}};
 const receiver={get list(){trace.push('list');if(mode==='list')throw new EvalError();return list;}};
 let result;try{result=m[name](receiver);}catch(e){result=e.constructor.name;}
 console.log(JSON.stringify({result,trace,gets}));
 `);
 const modes=['first','second','none','rebind','list','index','A','B','new.B'];
 const names=['check','andCheck'];
 const run=(file,mode,name)=>{const p=spawnSync(process.execPath,[runner,file,mode,name],{encoding:'utf8',timeout:10000});assert.equal(p.status,0,p.stderr);return p.stdout;};
 const expected=new Map();for(const name of names)for(const mode of modes) {
  const text=run(oracle,mode,name);assert.ok(!text.includes('valueOf'));expected.set(name+'/'+mode,text);
 }
 let comparisons=0,mutants=0;
 for(const [label,flags] of [['default',[]],['source',['--language-source']]]) {
  const out=join(dir,label),abc=join(fixtures,'candidates.abc');
  const exported=spawnSync(structure,[abc,out,'--value-source',...flags],{encoding:'utf8',timeout:30000});assert.equal(exported.status,0,exported.stderr);
  const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
  assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
  const file=join(out,records[0].source),text=readFileSync(file,'utf8');
  writeFileSync(join(dirname(file),'values.js'),values);
  const metadata=JSON.parse(readFileSync(join(out,records[0].metadata)));
  for(const name of names) {
   const method=metadata.methods.find(m=>m.runtime_name===name);assert.ok(method);
   const dis=spawnSync(semantic,[abc,String(method.entity),'--instructions'],{encoding:'utf8'});assert.equal(dis.status,0,dis.stderr);
   const ops=dis.stdout.trim().split('\n').map(JSON.parse);
   // Actual indexed read, two independent module loads and a conditional edge.
   assert.equal(ops.filter(i=>i.opcode===0x37).length,1);
   assert.equal(ops.filter(i=>i.opcode===0x7e).length,2);
   assert.ok(ops.some(i=>i.opcode===0x4f||i.opcode===0x51));
   for(const mode of modes){assert.equal(run(file,mode,name),expected.get(name+'/'+mode),label+'/'+name+'/'+mode);++comparisons;}
  }
  // Build valid JS mutants: duplicate the element read; merge later external
  // observations; reverse the short circuit. Each must change a strict trace.
  const changed=[text.replace(/(\$\w+\.list\[0\])/g,'($1, $1)'),text.replaceAll('state.B','state.A'),text.replaceAll(' || ',' && ')];
  for(const [i,mutant] of changed.entries()) {
   assert.notEqual(mutant,text);const path=join(dirname(file),'mutant'+i+'.mjs');writeFileSync(path,mutant);
   assert.ok(names.some(name=>modes.some(mode=>run(path,mode,name)!==expected.get(name+'/'+mode))));++mutants;
  }
 }
 console.log('snapshot_candidates_cli_test:',comparisons,'differentials,',mutants,'detected mutants');
}finally{rmSync(dir,{recursive:true,force:true});}
