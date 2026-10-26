import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, copyFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure, semantic, fixtures] = process.argv.slice(2);
const dir = mkdtempSync(join(tmpdir(), 'abcd-string-defaults-'));
const invoke = (exe,args) => {
  const r=spawnSync(exe,args,{encoding:'utf8',timeout:30000});
  assert.equal(r.status,0,r.stderr+'\n'+r.stdout);return r.stdout;
};
try {
  const runner=join(dir,'run.mjs');
  writeFileSync(runner,`
const m=await import(process.argv[2]);
const results=[];
for(const name of ['Logger','sequence','leading','arrow']) {
  for(let mode=0;mode<11;mode++) {
    const trace=[];
    const obj=n=>({toString(){trace.push(n);if(mode===10&&n===1)throw new RangeError();return 'obj'+n;}});
    const args=[[],[undefined],[undefined,undefined],[undefined,undefined,undefined],
      ['explicit'],['',0,''],[null,null,null],[1,2,3],
      [obj(0),obj(1),obj(2)],[undefined,obj(1),obj(2)],[obj(0),obj(1),obj(2)]][mode];
    let value,error;
    try {value=name==='Logger'?new m.Logger(...args).format():m[name](...args);}catch(e){error=e.constructor.name;}
    results.push({name,mode,length:m[name].length,value,error,trace});
  }
}
console.log(JSON.stringify(results));
`);
  const oracle=join(dir,'oracle.mjs');copyFileSync(join(fixtures,'defaults.js'),oracle);
  const run=file=>invoke(process.execPath,[runner,file]);
  const expected=run(oracle),values=JSON.parse(expected);
  assert.deepEqual([...new Map(values.map(v=>[v.name,v.length])).values()],[0,0,1,0]);
  for(const name of ['Logger','sequence','arrow']) {
    const group=values.filter(v=>v.name===name);
    assert.equal(group[0].value,group[1].value);
    assert.equal(group[1].value,group[2].value);
  }
  let mutations=0;
  for(const [label,flags] of [['default',[]],['source',['--language-source']]]) {
    const abc=join(fixtures,'defaults.abc'),out=join(dir,label);
    invoke(structure,[abc,out,'--value-source',...flags]);
    const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
    assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
    const metadata=JSON.parse(readFileSync(join(out,records[0].metadata)));
    for(const name of ['Logger','sequence','leading','arrow']) {
      const method=metadata.methods.find(m=>m.runtime_name===name);assert.ok(method);
      const dis=invoke(semantic,[abc,String(method.entity),'--instructions']).trim().split('\n').map(JSON.parse);
      // Pinned dynamic ISA: lda.str (pool-backed), ldai, strict equality and
      // conditional jump. The numeric default must follow the string default.
      const string=dis.find(i=>i.opcode===0x3e),number=dis.find(i=>i.opcode===0x62&&i.operands[0].bits===65280);
      assert.ok(string&&number&&string.pc<number.pc);
      assert.ok(dis.filter(i=>i.opcode===0x28).length>=2);
      assert.ok(dis.filter(i=>i.opcode===0x4f).length>=2);
    }
    const file=join(out,records[0].source),text=readFileSync(file,'utf8');
    invoke(process.execPath,['--check',file]);assert.equal(run(file),expected);
    const mutants=[text.replace('"LOG"','"WRONG"'),text.replaceAll('65280','65281'),
      text.replace('$arg0 = "LOG", $arg1 = 65280','$arg0 = 65280, $arg1 = "LOG"'),
      text.replace('$arg0 = "LOG"','$arg0')];
    for(const [i,mutant] of mutants.entries()) {
      assert.notEqual(mutant,text);const path=join(dir,`${label}-${i}.mjs`);writeFileSync(path,mutant);
      assert.notEqual(run(path),expected,'undetected mutation '+i);mutations++;
    }
  }
  console.log(`string_defaults_cli_test: 88 differential cases, ${mutations} detected mutants`);
} finally {rmSync(dir,{recursive:true,force:true});}
