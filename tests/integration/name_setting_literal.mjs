import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,copyFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import vm from 'node:vm';
import {createHash} from 'node:crypto';
const [structure,probe,printer,fixture]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-name-setting-'));
const run=(exe,args)=>{const r=spawnSync(exe,args,{encoding:'utf8',timeout:60000,maxBuffer:32*1024*1024});assert.equal(r.status,0,r.stderr);return r.stdout;};
try {
  const manifest=JSON.parse(readFileSync(join(fixture,'manifest.json')));
  for(const [path,hash] of Object.entries(manifest.hashes))
    assert.equal(createHash('sha256').update(readFileSync(join(fixture,path))).digest('hex'),hash);
  const rows=run(probe,[join(fixture,'names.abc')]).trim().split('\n').map(JSON.parse).filter(x=>x.type==='instruction');
  const encodings=new Set(rows.map(x=>x.encoding.opcode));
  for(const opcode of manifest.opcodes)assert.ok(encodings.has(opcode));
  for(const r of rows.filter(x=>manifest.opcodes.includes(x.encoding.opcode))) {
    assert.equal(r.lift.status,'ok');assert.equal(r.lift.roles.name_setting,1);
  }
  const runner=join(dir,'runner.mjs');
  writeFileSync(runner,`
const m=await import(process.argv[2]);const mode=process.argv[3];const trace=[];
let setter=0;const fk='f.\\\\name';
Object.defineProperty(Object.prototype,fk,{set(){setter++;},configurable:true});
let result,error;
const input={};for(const key of ['first','second','third'])Object.defineProperty(input,key,{get(){trace.push(key);if(mode===key)throw new RangeError();return trace.length;}});
try {const o=m.make(input);result=Object.entries(o).map(([k,v])=>{
const d=Object.getOwnPropertyDescriptor(o,k);const n=typeof v==='function'?Object.getOwnPropertyDescriptor(v,'name'):undefined;
return [k,typeof v==='function'?v.name:v,d.writable,d.enumerable,d.configurable,n&&[n.writable,n.enumerable,n.configurable]];});
result.push(o[fk](),o['a.\\\\name'](),new o['c.\\\\name']() instanceof o['c.\\\\name']);}
catch(e){error=e.constructor.name;}finally{delete Object.prototype[fk];}
const plain=m.ordinary();const wide=Object.entries(m.wide()).map(([k,v])=>[k,v.name,v()]);
console.log(JSON.stringify({result,error,trace,setter,plain:[plain.foo.name,plain.arrow.name],wide}));
`);
  const oracle=join(dir,'oracle.mjs');copyFileSync(join(fixture,'names.js'),oracle);
  const expected=new Map(['normal','first','second','third'].map(mode=>[mode,run(process.execPath,[runner,oracle,mode])]));
  for(const [label,flags] of [['default',[]],['v4',['--language-source']]]) {
    const output=join(dir,label);run(structure,[join(fixture,'names.abc'),output,'--value-source',...flags]);
    const records=JSON.parse(readFileSync(join(output,'manifest.json'))).records.filter(r=>r.source_status!==2);
    assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
    const source=join(output,records[0].source);run(process.execPath,['--check',source]);
    for(const [mode,want] of expected)assert.equal(run(process.execPath,[runner,source,mode]),want,`${label}/${mode}`);
    const text=readFileSync(source,'utf8');
    const mutated=join(dir,`${label}-renamed.mjs`);writeFileSync(mutated,text.replaceAll('function(', 'function renamed('));
    assert.notEqual(run(process.execPath,[runner,mutated,'normal']),expected.get('normal'));
  }
  for(const row of run(printer,['--property-names']).trim().split('\n').map(JSON.parse)) {
    const f=vm.runInNewContext(row.source)();assert.equal(f.name,row.key);
    assert.deepEqual(Object.getOwnPropertyDescriptor(f,'name'),{value:row.key,writable:false,enumerable:false,configurable:true});
  }
} finally {rmSync(dir,{recursive:true,force:true});}
