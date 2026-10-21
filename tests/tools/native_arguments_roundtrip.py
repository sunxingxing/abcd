#!/usr/bin/env python3
"""Native arguments source/engine round-trip, with strict assembly diffs retained.
No opcode/register/exception normalization; Node semantics is a separate oracle.
"""
import argparse,difflib,importlib.util,json,re,subprocess,sys,tempfile
from pathlib import Path
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('grouping_bytecode',Path(__file__).with_name('grouping_bytecode.py'))
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
p=argparse.ArgumentParser();p.add_argument('engine');p.add_argument('fixture',type=Path);p.add_argument('out',type=Path);p.add_argument('--temporary',action='store_true');a=p.parse_args()
if a.temporary:
 a.out.mkdir(parents=True,exist_ok=True);scratch=tempfile.TemporaryDirectory(dir=a.out);a.out=Path(scratch.name)/'roundtrip'
build=json.loads((a.fixture/'build.json').read_text())
assert g.sha(a.fixture/'entry.js')==build['source_sha256']
assert g.sha(a.fixture/'entry.abc')==build['abc_sha256']
a.out.mkdir(parents=True,exist_ok=False)
export=a.out/'export';subprocess.run([a.engine,str(a.fixture/'entry.abc'),str(export),'--value-source','--language-source'],check=True,capture_output=True)
r=next(r for r in json.loads((export/'manifest.json').read_text())['records'] if r['source_status']!=2)
assert r['source_status']==0,r
report={'compiler':{'path':g.ES2ABC,'sha256':g.sha(g.ES2ABC)},'disassembler':{'path':g.DISASM,'sha256':g.sha(g.DISASM)},'modes':{}}
for mode in ['default','O0']:
 texts={};commands={};adaptations={}
 for side,source in [('original',a.fixture/'entry.js'),('emitted',export/r['source'])]:
  d=a.out/(side+'-'+mode);d.mkdir();src=d/'input.js';src.write_text(source.read_text())
  adaptation = subprocess.run(["node", "--expose-internals", "--input-type=module", "-e", r"""
  import {readFileSync,writeFileSync} from 'node:fs';
  import {createRequire} from 'node:module';
  const acorn=createRequire(import.meta.url)('internal/deps/acorn/acorn/dist/acorn');
  const file=process.argv[1];let source=readFileSync(file,'utf8');
  const ast=acorn.parse(source,{ecmaVersion:'latest',sourceType:'module'}),edits=[];
  for(const n of ast.body)if(n.type==='ExportNamedDeclaration')for(const spec of n.specifiers){
   if(spec.exported.type!=='Literal'||typeof spec.exported.value!=='string')continue;
   const name=spec.exported.value;
   try{const a=acorn.parse(`const x=0;export {x as ${name}};`,{ecmaVersion:'latest',sourceType:'module'});
   if(a.body[1].specifiers[0].exported.type==='Identifier')edits.push([spec.exported.start,spec.exported.end,name]);}catch{}
  }
  for(const [a,b,text] of edits.sort((a,b)=>b[0]-a[0]))source=source.slice(0,a)+text+source.slice(b);
  writeFileSync(file,source);console.log(edits.length);
  """, str(src)], check=True, capture_output=True, text=True)
  adaptations[side]=int(adaptation.stdout)
  cmd=[g.ES2ABC,'--module','--merge-abc','--record-name=arguments_acceptance','--source-file=input.js',*(['--opt-level=0'] if mode=='O0' else []),'--output='+str(d/'input.abc'),str(src)]
  subprocess.run(cmd,check=True,capture_output=True);subprocess.run([g.DISASM,str(d/'input.abc'),str(d/'input.pa')],check=True,capture_output=True)
  texts[side]=g.assembly(d/'input.pa');commands[side]=cmd
 diff=''.join(difflib.unified_diff(texts['original'].splitlines(True),texts['emitted'].splitlines(True)));(a.out/(mode+'.diff')).write_text(diff)
 methods=lambda text:re.findall(r'(?ms)^\.function .*?^}\n',text)
 original,emitted=methods(texts['original']),methods(texts['emitted'])
 report['modes'][mode]={'equal':texts['original']==texts['emitted'],'export_alias_adaptations':adaptations,'commands':commands,'original_snapshot_count':texts['original'].count('\tgetunmappedargs'),'emitted_snapshot_count':texts['emitted'].count('\tgetunmappedargs'),'methods':[{'original':x.splitlines()[0],'emitted':y.splitlines()[0],'equal':x==y} for x,y in zip(original,emitted)],'method_counts':[len(original),len(emitted)]}
 recompiled=a.out/('recompiled-'+mode)
 subprocess.run([a.engine,str(a.out/('emitted-'+mode)/'input.abc'),str(recompiled),'--value-source','--language-source'],check=True,capture_output=True)
 recompiled_records=[r for r in json.loads((recompiled/'manifest.json').read_text())['records'] if r['source_status']!=2]
 report['modes'][mode]['recompiled_complete']=all(r['source_status']==0 for r in recompiled_records)
 report['modes'][mode]['recompiled_failures']=[r.get('source_failure') for r in recompiled_records if r['source_status']!=0]
 assert len(original)==len(emitted)
 assert report['modes'][mode]['original_snapshot_count']==report['modes'][mode]['emitted_snapshot_count']
(a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
