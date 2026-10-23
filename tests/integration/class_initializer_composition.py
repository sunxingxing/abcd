#!/usr/bin/env python3
"""Native static initialization: real ABC, exporter, Node and SDK evidence.

Refused instance/computed/static-block fixtures are explicit coverage gaps, never
counted as engine differentials. Disassembly differences are retained verbatim.
"""
import difflib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'support/isa'))
from oracle_evidence import record_node

engine, fixtures, directory = sys.argv[1:]
fixtures = Path(fixtures).resolve()
root = Path(directory).resolve()
root.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='run-', dir=root))
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('grouping', Path(__file__).parents[1]/'tools/grouping_bytecode.py')
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)
published = {'private_receiver', 'installed_accessors', 'inner_outer', 'reentry', 'outer_reassigned'}
refusals = {
    'instance_base': [('abcd::source::CreationContextError', 3)],
    'instance_derived': [('abcd::source::CreationContextError', 3)],
    'computed': [('abcd::analysis::SemanticError', 1)],
    'static_block': [('abcd::source::TreePrintError', 0)],
    'public_tdz': [],
}
runner = r'''
import {pathToFileURL} from 'node:url';
const mode=process.argv[2], log=[];
Object.defineProperty(globalThis,'event',{configurable:true,get(){
  log.push('event-get');if(mode==='event-get-throw')throw new SyntaxError('event-get');
  return value => {log.push(value);if(mode==='event-throw')throw new RangeError('event');return value;};
}});
Object.defineProperty(globalThis,'seed',{configurable:true,get(){
  log.push('seed-get');if(mode==='seed-get-throw')throw new SyntaxError('seed-get');
  return () => {log.push('seed');if(mode==='seed-throw')throw new TypeError('seed');
    if(mode==='replace-map')globalThis.Map=class {constructor(){log.push('wrong-map');}};
    return [['a',7]];};
}});
globalThis.peek = read => {log.push('peek');return read();};
if(mode==='map-throw')globalThis.Map=class {constructor(){log.push('map');throw new SyntaxError('map');}};
if(mode==='setter')Object.defineProperty(Function.prototype,'more',{configurable:true,set(){log.push('inherited-set');}});
let result;
try {
  const module=await import(pathToFileURL(process.argv[1]));
  const C=module.originalClass || module.C;
  const c=new C();
  result={name:C.name,length:C.length,value:c.value,x:C.x===C?'self':C.x,
    y:C.y,read:C.read?C.read()===C:undefined,
    more:C.more,context:C.context?[...C.context]:undefined,
    publicValue:module.originalClass?module.C:undefined,keys:Object.keys(C),descriptor:Object.getOwnPropertyDescriptor(C,'x')};
  if(result.descriptor && result.descriptor.value===C)result.descriptor.value='self';
  if('read' in c){c.value=8;result.after=c.read();}
} catch(e){result={error:e.constructor.name};}
console.log(JSON.stringify({result,log}));
'''
report = {'compiler': {'path':sdk.ES2ABC,'sha256':sdk.sha(sdk.ES2ABC)},
          'disassembler':{'path':sdk.DISASM,'sha256':sdk.sha(sdk.DISASM)},
          'fixtures':[], 'node_differentials':0}
for source in sorted(fixtures.glob('*.js')):
    name=source.stem
    out=root/name
    out.mkdir()
    original=out/'original.mjs'
    original.write_text(source.read_text())
    row={'fixture':name,'compilations':[]}
    for mode in ('default','O0'):
        abc=out/(mode+'.abc')
        command=[sdk.ES2ABC,'--module','--merge-abc',f'--record-name={name}',
                 '--source-file=input.js','--extension=js',f'--output={abc}']
        if mode=='O0':command.append('--opt-level=0')
        command.append(str(source))
        assert sdk.run(command, out/(mode+'.compile.log'))==0
        exported=out/(mode+'-export')
        subprocess.run([engine,str(abc),str(exported),'--value-source','--language-source'],
                       check=True,capture_output=True)
        records=[r for r in json.loads((exported/'manifest.json').read_text())['records']
                 if r.get('source_status') in (0,3)]
        assert len(records)==1,(name,records)
        record=records[0]
        result={'mode':mode,'command':command,'abc_sha256':sdk.sha(abc),'published':record['source_status']==0}
        expected = refusals.get(name)
        # The default builds' `sta; poplexenv; lda; throw` cleanup handlers
        # (code_size 0) are accepted since 2026-10-23: inner_outer, reentry
        # and outer_reassigned complete and take the differentials below.
        if mode == 'default' and name in {'instance_base','instance_derived'}:
            expected = [('abcd::source::CreationContextError', 3)]
        # public_tdz: with copy-separated module guards paired, the default
        # build reaches the same record-program refusal as O0.
        if expected is not None:
            assert record['source_status']==3,(name,record)
            causes=record.get('source_failure',{}).get('causes',record.get('source_causes',[]))
            assert [(c['domain'],c['value']) for c in causes]==expected,(name,record)
            if name == 'public_tdz':
                assert record['source_failure_domain']=='record_program' and record['source_error']==7
            result['refusal']=causes
            row['compilations'].append(result)
            continue
        assert result['published'],(name,record)
        emitted=exported/record['source']
        subprocess.run(['node','--check',str(emitted)],check=True,capture_output=True)
        for scenario in ('plain','setter','seed-throw','event-throw','map-throw','seed-get-throw','event-get-throw','replace-map'):
            values=[subprocess.run(['node','--input-type=module','-e',runner,str(s),scenario],
                                  check=True,capture_output=True,text=True).stdout
                    for s in (original,emitted)]
            assert values[0]==values[1],(name,mode,scenario,values)
            report['node_differentials']+=1
        record_node(abc, exported, original, Path(__file__), 8, engine)
        adapted=out/(mode+'-emitted.js')
        adapted.write_text(re.sub(r' as "([A-Za-z_$][\w$]*)"',r' as \1',emitted.read_text()))
        newabc=out/(mode+'-emitted.abc')
        recompile=[*command[:-1]]
        recompile=[f'--output={newabc}' if x.startswith('--output=') else x for x in recompile]
        recompile.append(str(adapted))
        assert sdk.run(recompile,out/(mode+'-emitted.compile.log'))==0
        texts=[]
        for side,path in [('original',abc),('emitted',newabc)]:
            pa=out/(mode+'-'+side+'.pa')
            assert sdk.run([sdk.DISASM,str(path),str(pa)],out/(mode+'-'+side+'.disasm.log'))==0
            texts.append(sdk.assembly(pa))
        methods=[dict(re.findall(r'^(\.function [^\n]+)\n(.*?^})',t,re.M|re.S)) for t in texts]
        result.update(recompile=recompile,identical=texts[0]==texts[1],methods=[
            {'method':m,'identical':methods[0].get(m)==methods[1].get(m)}
            for m in sorted(methods[0].keys()|methods[1].keys())])
        (out/(mode+'.diff')).write_text(''.join(difflib.unified_diff(texts[0].splitlines(True),texts[1].splitlines(True),fromfile='original',tofile='emitted')))
        row['compilations'].append(result)
    report['fixtures'].append(row)
(root/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'fixtures':len(report['fixtures']),'published':len(published),
                  'refused':len(refusals),'node_differentials':report['node_differentials'],
                  'sdk_exact_records':sum(c.get('identical',False) for r in report['fixtures'] for c in r['compilations']),
                  'report':str(root/'summary.json')}))
