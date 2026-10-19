#!/usr/bin/env python3
"""Current-ISA evidence only. Decode/lift evidence never implies execution."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tests/support/abc'))
from builder import Module, Instruction, Label, build, BuildError
BUILD = ROOT/'build/regression'
FIX = ROOT/'tests/fixtures/isa/current'
SPEC = ROOT/'tests/isa/current_variants.json'
SDK = Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony')
COMPILER = SDK/'ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc'
DISASM = SDK/'toolchains/ark_disasm'
RUNTIME = ROOT/'references/openharmony/arkcompiler/ets_runtime'
FRONT = ROOT/'references/openharmony/arkcompiler/ets_frontend/es2panda'

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def run(args): return subprocess.run([str(a) for a in args],capture_output=True,text=True,timeout=90,cwd=ROOT)
def catalog():
    names={int(v):n for n,v in re.findall(r'\bk(\w+)\s*=\s*(\d+)',(BUILD/'generated/abcd/abc/generated_isa.h').read_text())}
    return {names[r['opcode']]:r for line in run([BUILD/'isa_coverage_probe','--catalog']).stdout.splitlines() if (r:=json.loads(line))}
def probe(p):
    r=run([BUILD/'isa_coverage_probe',p]); assert r.returncode==0,(p,r.stderr)
    return [x for line in r.stdout.splitlines() if (x:=json.loads(line))['type']=='instruction']
def specimen(s):
    # Packed nibbles and all operand widths come from the generated ISA catalog.
    # Pool method 0 is real; string 1 is "p"; literal 2 is an empty tagged array.
    bits=0;shift=0
    branch=s['name'] in ('JMP_IMM32','JEQZ_IMM32','JNEZ_IMM32')
    for f in ([] if branch else s['format']):
        v=1 if f['kind']==5 or (s['name']=='WIDE_NEWOBJRANGE_PREF_IMM16_V8' and f['kind']==1) else 2 if f['kind']==6 else 0
        bits |= v << shift;shift+=f['width']
    operands=bits.to_bytes((shift+7)//8,'little')
    i=Instruction(s['opcode'],operands,Label(0) if branch else None,4 if branch else 0,s['size'] - (4 if branch else len(operands)))
    c=catalog()
    return Module((Instruction(c['LDUNDEFINED']['opcode']),i,Label(0),Instruction(c['RETURNUNDEFINED']['opcode'])),registers=8,pool_strings=('p',),empty_literals=1)
def citations():
    # This index locates evidence only. It never controls production admission.
    files=[RUNTIME/'ecmascript/interpreter/interpreter-inl.cpp',RUNTIME/'ecmascript/compiler/interpreter_stub.cpp',RUNTIME/'ecmascript/interpreter/interpreter_assembly.cpp',RUNTIME/'ecmascript/compiler/baseline/baseline_compiler.cpp',RUNTIME/'ecmascript/compiler/bytecode_circuit_builder.cpp']
    return {str(p.relative_to(ROOT)):p.read_text() for p in files}
def evidence(name,texts):
    rt='references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp'
    s=texts[rt];m=re.search(r'HANDLE_OPCODE\('+name+r'\)',s);assert m,name
    end=s.find('HANDLE_OPCODE(',m.end());body=s[m.start():end if end>=0 else len(s)]
    status='fatal' if 'LOG_FULL(FATAL)' in body else 'no_op' if name=='NOP' else 'implemented'
    refs=[{'path':rt,'line':s[:m.start()].count('\n')+1,'sha256':sha(ROOT/rt),'status':status}]
    camel='Handle'+''.join(x.capitalize() for x in name.split('_'))
    for p,t in texts.items():
        if p==rt:continue
        pat = (r'DECLARE_ASM_HANDLER\('+camel+r'\)' if p.endswith('interpreter_stub.cpp') else
               r'void InterpreterAssembly::'+camel+r'\(' if p.endswith('interpreter_assembly.cpp') else
               r'BYTECODE_BASELINE_HANDLER_IMPLEMENT\('+name+r'\)' if p.endswith('baseline_compiler.cpp') else r'case EcmaOpcode::'+name+r':')
        match=re.search(pat,t)
        if match:refs.append({'path':p,'line':t[:match.start()].count('\n')+1,'sha256':sha(ROOT/p)})
    token=''.join(name.split('_')[:2] if name.startswith(('WIDE_','CALLRUNTIME_')) else name.split('_')[:1]).lower()
    sites=[]
    for p in sorted((FRONT/'compiler').rglob('*.cpp')):
        for line,l in enumerate(p.read_text().splitlines(),1):
            if any(x.lower()==token for x in re.findall(r'Emit(?:Range)?<(\w+)>',l)):
                sites.append({'path':str(p.relative_to(ROOT)),'line':line,'text':l.strip()})
    return {'interpreter':status,'runtime':refs,'frontend_emit_sites':sites,
            'emission_limit':'A frontend emitter is not proof that this format or mode is selected; actual probes are separate.'}
def sources():
    result = {
      'expressions':("export function f(a,b,o,k,t){ debugger; const r=/a+b/giu; const z=123456789012345678901234567890n; const q=t`hello${a}world`; const x={__proto__:o,...b}; const {p,...rest}=o; return [a^b,a**b,~a,+a,NaN,Infinity,Symbol,typeof a,r,z,q,x,rest,arguments,new.target]; }\nexport async function d(s){return import(s);}\n",['--module']),
      'generators':("export function* g(x){yield x;return yield* x;}\nexport async function* ag(x){yield await x;return yield* x;}\nexport async function ai(x){for await(const a of x){if(a)return a;}return 0;}\n",['--module']),
      'calls':("export function f(g,o,a){g(...a);o.m(...a);return new g(...a);}\nexport class A{constructor(...a){this.a=a;}}\nexport class B extends A{constructor(...a){const f=()=>super(...a);f();} m(k,v){super[k]=v;super.p=v;return super[k]+super.p;}}\n",['--module']),
      'private':("export class A{#x=1;#m(){return this.#x;} static #y=2;get(o){return #x in o ? o.#m():A.#y;}set(v){this.#x=v;}init=()=>this.#x;}\n",['--module','--use-define-semantic']),
      'globals':("var a=1;let b=2;const c=3;function f(x){a=x;b=x;undeclared=x;return a+b+c+undeclared+NaN+Infinity;}\n",[]),
      'exceptions':("export function f(o){const x=1;x=2;const {}=o;return x;} export class A{m(){delete super.x;}}\n",['--module']),
      'wide_ic':("export class A{}\nexport class B extends A{m(o,k,v){\n"+'\n'.join('typeof o; super.p; super[k]; super.p=v; super[k]=v; o[k]=v;' for _ in range(150))+"\nreturn o;}}\n",['--module']),
      'wide_range':("export function f(g){g("+','.join(str(i) for i in range(270))+"); return new g("+','.join(str(i) for i in range(270))+");}\nexport class B extends Object{constructor(){super("+','.join(str(i) for i in range(270))+");}}\n",['--module']),
      'wide_cells':("export function f(){"+''.join(f'let v{i}={i};' for i in range(270))+"return ()=>["+','.join(f'v{i}' for i in range(270))+"]; }\n",['--module','--debug-info']),
      'wide_module':(''.join(f"import {{x as v{i}}} from 'm{i}';\nexport let e{i}=v{i};\n" for i in range(260))+"import * as ns from 'namespace';export function f(){return [ns,"+','.join(f'e{i}' for i in range(260))+"]; }\n",['--module']),
      'lazy':("import lazy {x} from 'lazy'; export function f(){return x;}\n",['--module','--extension=ts']),
      'sendable':("import {x} from 'm'; export class A{ n:number;constructor(){'use sendable';this.n=1;} m(){return x;}} export function f(){class B{constructor(){'use sendable';} m(){return B;}}return B;}\n",['--module','--extension=ts']),
      'lazy_sendable':("import lazy {x} from 'm'; export class A{constructor(){'use sendable';}m(){return x;}}\n",['--module','--extension=ts']),
      'shared':("'use shared'; export class A{constructor(){'use sendable';} m(){return A;}}export const a=new A();\n",['--module','--extension=ts']),
      'debugger':("let x=1;function f(){debugger;return x;}f();\n",['--debug-info','--debugger-evaluate-expression']),
      'namespace':("import * as ns from 'm';export function f(){return ns;}\n",['--module']),
      'patch':("let x=1;function f(){x++;return x;}f();\n",['--hot-reload']),
    }
    result['api12']=(result['expressions'][0],['--module','--target-api-version=12'])
    result['wide_lazy']=(''.join(f"import lazy {{x as v{i}}} from 'm{i}';\n" for i in range(260))+"export function f(){return ["+','.join(f'v{i}' for i in range(260))+"]; }\nexport class A{constructor(){'use sendable';}m(){return ["+','.join(f'v{i}' for i in range(260))+"];}}\n",['--module','--extension=ts'])
    result['wide_sendable']=(''.join(f"import {{x as v{i}}} from 'm{i}';\nexport const e{i}=v{i};\n" for i in range(260))+"export class A{constructor(){'use sendable';}m(){return ["+','.join(f'v{i},e{i}' for i in range(260))+"];}}\n",['--module','--extension=ts'])
    result['wide_named_call']=("export function f(o){return o.m("+','.join(str(i) for i in range(270))+");}\n",['--module','--enable-callable-name'])
    result['wide_rest']=("export function f("+','.join(f'a{i}' for i in range(260))+",...tail){return tail;}\n",['--module'])
    result['wide_patterns']=("export function f(o,k,t,C){const {"+','.join(f'k{i}' for i in range(260))+",...rest}=o;\n"+'\n'.join('t`a`; ({__proto__:o,[k]:function(){}}); new C(...o); for(const a of o){break;}' for _ in range(150))+"return rest;}\n",['--module'])
    result['wide_jump']=("export function f(o,c){if(c){"+'o.p();'*9000+"}else{"+'o.q();'*9000+"}return o;}\n",['--module'])
    return result
def generate():
    FIX.mkdir(parents=True,exist_ok=True)
    original=json.loads((ROOT/'build/isa-coverage/coverage.json').read_text())
    rows=[dict(r,**{}) for r in original['rows'] if r['classification']=='Unmeasured' and not r['name'].startswith('DEPRECATED_')]
    if SPEC.exists():rows=json.loads(SPEC.read_text())['variants'] # fixed baseline selection
    entries=[];texts=citations();current=catalog()
    with tempfile.TemporaryDirectory() as tmp:
      for r in rows:
        name=r['name'];spec={k:(name if k=='name' else current[name][k]) for k in ('name','opcode','format','prefix','size')}
        path=FIX/(name.lower()+'.abc');path.write_bytes(build(specimen(spec))[0])
        dis=run([DISASM,path,Path(tmp)/'out.pa']);assert dis.returncode==0,(name,dis.stderr)
        decoded=probe(path);hit=[x for x in decoded if x['encoding']['opcode']==r['opcode']];assert len(hit)==1,(name,decoded)
        entries.append({'path':path.name,'origin':'constructed','sha256':sha(path),'name':name,'opcode':r['opcode'],'profile':hit[0]['profile'],'lift':hit[0]['lift'],'encoding':hit[0]['encoding']})
        spec['evidence']=evidence(name,texts)
        spec['impact']=[{'profile':p['profile'],'cohort_records_containing':p['cohort_records_containing'],'current_first_refusal':p['current_first_refusal']} for p in r.get('profiles',[])] or r.get('impact',[])
        rows[rows.index(r)]=spec
        print('constructed',name,flush=True)
    attempts=[]
    for name,(text,options) in sources().items():
      src=FIX/(name+('.ets' if '--extension=ts' in options else '.js'));src.write_text(text)
      for opt,extra in [('o0',['--opt-level=0']),('default',[])]:
        path=FIX/f'{name}-{opt}.abc';args=[COMPILER,'--merge-abc',*options,*extra,'--output='+str(path),src]
        r=run(args);attempt={'source':src.name,'source_sha256':sha(src),'command':[str(a) for a in args],'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr}
        if r.returncode==0:
          observed=probe(path);attempt.update(path=path.name,sha256=sha(path),opcodes=sorted({v['encoding']['opcode'] for v in observed}))
          entries.append({'path':path.name,'origin':'es2abc','sha256':sha(path),'opcodes':attempt['opcodes']})
        attempts.append(attempt);print('compiler',name,opt,r.returncode,flush=True)
    SPEC.write_text(json.dumps({'schema':1,'baseline':'7d97d2a6','scope':'127 current Unmeasured variants; 47 deprecated deferred','variants':rows},indent=2)+'\n')
    (FIX/'manifest.json').write_text(json.dumps({'schema':1,'compiler':str(COMPILER),'compiler_sha256':sha(COMPILER),'disassembler':str(DISASM),'disassembler_sha256':sha(DISASM),'fixtures':entries,'attempts':attempts},indent=2)+'\n')
    registry_path=ROOT/'tests/isa/coverage_manifest.json';registry=json.loads(registry_path.read_text());prefix='tests/fixtures/isa/current/'
    registry['fixtures']=[r for r in registry['fixtures'] if not r['path'].startswith(prefix)]
    for e in entries:registry['fixtures'].append({'path':prefix+e['path'],'sha256':e['sha256'],'profile':e.get('profile',6),'opcodes':[e['opcode']] if e['origin']=='constructed' else [],'classification':'Unmeasured','oracles':{},'note':'Decode/raw lift only; arbitrary operands are not an executable protocol or source oracle.'})
    registry_path.write_text(json.dumps(registry,indent=2)+'\n')
class CurrentEvidence(unittest.TestCase):
    def test_all_current(self):
      specs=json.loads(SPEC.read_text())['variants'];self.assertEqual(len(specs),127)
      manifest=json.loads((FIX/'manifest.json').read_text());self.assertEqual(sha(COMPILER),manifest['compiler_sha256'])
      with tempfile.TemporaryDirectory() as tmp:
       for e in manifest['fixtures']:
        p=FIX/e['path'];self.assertEqual(sha(p),e['sha256']);rows=probe(p)
        dis=run([DISASM,p,Path(tmp)/'out.pa']);self.assertEqual(dis.returncode,0,dis.stderr)
        if e['origin']=='constructed':
          s=next(s for s in specs if s['opcode']==e['opcode']);self.assertEqual(p.read_bytes(),build(specimen(s))[0])
          hit=[r for r in rows if r['encoding']['opcode']==e['opcode']];self.assertEqual(len(hit),1)
          self.assertEqual(hit[0]['encoding'],e['encoding']);self.assertEqual(hit[0]['encoding']['prefix'],catalog()[e['name']]['prefix']);self.assertEqual(hit[0]['lift'],e['lift'])
          for ref in s['evidence']['runtime']:self.assertEqual(sha(ROOT/ref['path']),ref['sha256'])
        else:self.assertEqual(sorted({r['encoding']['opcode'] for r in rows}),e['opcodes'])
    def test_frontend_reproduction(self):
      manifest=json.loads((FIX/'manifest.json').read_text())
      with tempfile.TemporaryDirectory() as tmp:
       for a in manifest['attempts']:
        self.assertEqual(sha(FIX/a['source']),a['source_sha256'])
        args=list(a['command'])
        args=[('--output='+str(Path(tmp)/a['path'])) if x.startswith('--output=') else x for x in args]
        r=run(args);self.assertEqual(r.returncode,a['exit'],r.stderr)
        self.assertEqual(sha(Path(tmp)/a['path']),a['sha256'])
    def test_budget_and_widths(self):
      specs=json.loads(SPEC.read_text())['variants'];s=next(s for s in specs if s['name']=='CALLRUNTIME_STSENDABLEVAR_PREF_IMM4_IMM4')
      for count in (200,201,400,800):
        m=specimen(s);m=Module(m.instructions*count,registers=8,pool_strings=('p',),empty_literals=1)
        # Remove repeated labels: this specimen has no branch and labels serve no role.
        m=Module(tuple(i for i in m.instructions if not isinstance(i,Label)),registers=8,pool_strings=('p',),empty_literals=1)
        expected=build(m)[0];lo,hi=0,1024*1024
        while lo<hi:
          mid=(lo+hi)//2
          try:build(m,mid);hi=mid
          except BuildError:lo=mid+1
        self.assertEqual(build(m,lo)[0],expected)
        with self.assertRaises(BuildError):build(m,lo-1)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--generate',action='store_true');args=p.parse_args()
    if args.generate:generate()
    else:unittest.main(argv=[sys.argv[0]])
