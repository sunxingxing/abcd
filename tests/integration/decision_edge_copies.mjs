import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,copyFileSync,rmSync} from 'node:fs';
import {join,dirname} from 'node:path';
import {tmpdir} from 'node:os';
const [structure,semantic,fixtures]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-decision-edges-'));
try {
 const oracle=join(dir,'oracle.mjs');copyFileSync(join(fixtures,'edges.js'),oracle);
 const runner=join(dir,'runner.mjs');
 writeFileSync(runner,`
 import {pathToFileURL} from 'node:url';
 const m=await import(pathToFileURL(process.argv[2]));
 const name=process.argv[3],mode=process.argv[4],trace=[];let iteration=0;
 const get=(key,value)=>{trace.push(key);if(mode===key)throw new RangeError(key);return value;};
 const truth={valueOf(){trace.push('valueOf');throw new Error();}};
 const o={get initial(){return get('initial',11)},get other(){return get('other',23)},
 get a(){return get('a',mode==='first'||mode==='truth'?truth:false)},
 get b(){return get('b',mode==='none'?false:true)},get value(){return get('value',37)},
 get effect(){return get('effect',0)},get more(){return get('more',iteration++<3)},
 observe(a,b){trace.push(['observe',a,b]);if(mode==='observe')throw new SyntaxError();},
 caught(name){trace.push(['caught',name]);}};
 let result;try{result=m[name](o);}catch(e){result=e.constructor.name;}
 console.log(JSON.stringify({result,trace}));
 `);
 const names=['check','andCheck','handled','swap'];
 const modes=['first','second','none','truth','initial','a','b','value','effect','other','more','observe'];
 const run=(file,name,mode)=>{const p=spawnSync(process.execPath,[runner,file,name,mode],{encoding:'utf8',timeout:10000});assert.equal(p.status,0,p.stderr);return p.stdout;};
 const expected=new Map();for(const name of names)for(const mode of modes){const text=run(oracle,name,mode);assert.ok(!text.includes('valueOf'));expected.set(name+'/'+mode,text);}
 let comparisons=0,mutants=0;
 for(const [label,flags] of [['default',[]],['source',['--language-source']]]){
  const out=join(dir,label),abc=join(fixtures,'edges.abc');
  const exported=spawnSync(structure,[abc,out,'--value-source',...flags],{encoding:'utf8',timeout:30000});assert.equal(exported.status,0,exported.stderr);
  const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
  assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
  const file=join(out,records[0].source),text=readFileSync(file,'utf8');
  // This is the new path, not a fixture that happens to avoid decision copies.
  assert.ok((text.match(/\? true :/g)||[]).length>=3);
  const metadata=JSON.parse(readFileSync(join(out,records[0].metadata)));
  for(const name of names){
   const method=metadata.methods.find(m=>m.runtime_name===name);assert.ok(method);
   const dis=spawnSync(semantic,[abc,String(method.entity),'--instructions'],{encoding:'utf8'});assert.equal(dis.status,0,dis.stderr);
   const ops=dis.stdout.trim().split('\n').map(JSON.parse);
   assert.ok(ops.some(i=>i.opcode===0x42));assert.ok(ops.some(i=>i.opcode===0x4f||i.opcode===0x51));
   if(name==='handled')assert.ok(ops.some(i=>i.exception_edge?.handler_pc!==undefined));
   for(const mode of modes){assert.equal(run(file,name,mode),expected.get(name+'/'+mode),label+'/'+name+'/'+mode);++comparisons;}
  }
  const changed=[text.replace(/\$phi\d+ = \$value\d+/g,'0'),text.replace(/\$phi\d+ = \$value\d+/g,match=>match.replace(/\$value\d+/, 'null')),text.replaceAll(' || ',' && ')];
  for(const [i,mutant] of changed.entries()){
   assert.notEqual(mutant,text);const path=join(dirname(file),'mutant'+i+'.mjs');writeFileSync(path,mutant);
   assert.ok(names.some(name=>modes.some(mode=>run(path,name,mode)!==expected.get(name+'/'+mode))));++mutants;
  }
 }
 console.log('decision_edge_copies_cli_test:',comparisons,'differentials,',mutants,'detected mutants');
}finally{rmSync(dir,{recursive:true,force:true});}
