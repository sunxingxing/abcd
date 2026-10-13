// Node checks a deliberately eager adaptation, NOT Ark lazy initialization.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, mkdirSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure, semantic, fixtures] = process.argv.slice(2);
const root=mkdtempSync(join(tmpdir(),'abcd-lazy-import-'));
const run=(bin,args)=>{const r=spawnSync(bin,args,{encoding:'utf8'});assert.equal(r.status,0,r.stderr);return r.stdout;};
try {
  const exported=(name,flags=['--language-source'])=>{
    const out=join(root,`${name}-${flags.join('-')}`);
    run(structure,[join(fixtures,`${name}.abc`),out,'--value-source',...flags]);
    const manifest=JSON.parse(readFileSync(join(out,'manifest.json'),'utf8'));
    const record=manifest.records.find(r=>r.source_status!==2);
    assert.ok(record);return {out,record};
  };
  const main=exported('main'),dep=exported('dep');
  for(const item of [main,dep])assert.equal(item.record.source_status,0,JSON.stringify(item.record));
  assert.equal(main.record.source_language,'arkts');assert.match(main.record.source,/\.ets$/);
  const text=readFileSync(join(main.out,main.record.source),'utf8');
  const lazy=(s)=>assert.match(s,/import lazy \{/);
  lazy(text);assert.throws(()=>lazy(text.replaceAll('import lazy ','import ')));
  const meta=JSON.parse(readFileSync(join(main.out,main.record.metadata),'utf8'));
  const phase=meta.record_fields.find(f=>f.name==='moduleRequestPhaseIdx');assert.ok(phase);
  const abc=readFileSync(join(fixtures,'main.abc'));
  const offset=phase.value.bits;assert.equal(abc.readUInt32LE(offset),meta.module.requests.length);
  assert.ok(abc.subarray(offset+4,offset+4+meta.module.requests.length).every(v=>v!==0));
  // Corrupt only a temporary copy; recompute the ABC Adler-32 boundary.
  const checksum=bytes=>{
    let a=1,b=0;for(const byte of bytes.subarray(12)){a=(a+byte)%65521;b=(b+a)%65521;}
    bytes.writeUInt32LE(((b<<16)|a)>>>0,8);
  };
  for(const [kind,edit,domain,value] of [
    ['count',b=>b.writeUInt32LE(meta.module.requests.length+1,offset),'ModuleError',7],
    ['eager',b=>{b[offset+4]=0;},'ModuleReadError',7],
  ]) {
    const bad=Buffer.from(abc);edit(bad);checksum(bad);
    const input=join(root,kind+'.abc'),out=join(root,kind+'-out');writeFileSync(input,bad);
    run(structure,[input,out,'--value-source','--language-source']);
    const rejected=JSON.parse(readFileSync(join(out,'manifest.json'),'utf8')).records.find(r=>r.source_status!==2);
    assert.equal(rejected.source_complete,false);
    const causes=rejected.source_failure?.causes??rejected.source_causes;
    assert.ok(causes?.some(c=>c.domain.endsWith(domain)&&c.value===value),JSON.stringify(rejected));
  }
  let loads=0;
  for(const method of meta.methods){
    const rows=run(semantic,[join(fixtures,'main.abc'),String(method.entity),'--instructions']).trim().split('\n').map(JSON.parse);
    for(const row of rows)if(row.opcode===0x15fb||row.opcode===0x16fb){++loads;assert.equal(row.semantic_error,undefined);assert.equal(row.exceptional_completion,1);}
  }
  assert.ok(loads>=4);
  for(const flags of [[],['--language-numeric','--language-constructors']]){
    const old=exported('main',flags).record;assert.equal(old.source_status,3);
    assert.ok((old.source_failure?.causes ?? old.source_causes)?.some(c=>c.domain.endsWith('ModuleEnclosureError') && c.value===10),JSON.stringify(old));
  }
  const runner=`globalThis.lazyEvents=[];const m=await import('./main.mjs');console.log(JSON.stringify([m.read(),m.change(),m.read(),m.caught(),globalThis.lazyEvents]));`;
  const evaluate=(name,mainText,depText)=>{
    const dir=join(root,name);mkdirSync(dir);writeFileSync(join(dir,'main.mjs'),mainText.replaceAll('import lazy ','import '));
    writeFileSync(join(dir,'dep.mjs'),depText);writeFileSync(join(dir,'run.mjs'),runner);
    return run(process.execPath,[join(dir,'run.mjs')]).trim();
  };
  const depText=readFileSync(join(dep.out,dep.record.source),'utf8');
  const expected=evaluate('original',readFileSync(join(fixtures,'main.ts'),'utf8'),readFileSync(join(fixtures,'dep.ts'),'utf8'));
  assert.equal(evaluate('recovered',text,depText),expected);
  assert.deepEqual(JSON.parse(expected),[1,2,2,2,['dep','main']]);
  // A frozen import value must be detected independently of printed spelling.
  const snapshot=`const frozen=value;\n`+text.replace(/return value;/g,'return frozen;');
  assert.notEqual(evaluate('snapshot',snapshot,depText),expected);
} finally {rmSync(root,{recursive:true,force:true});}
