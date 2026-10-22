#!/usr/bin/env python3
"""Owner-bound frame reads; constructed ABCs are independent of JS oracles."""
import json, re, subprocess, sys, tempfile, unittest
from dataclasses import replace
from functools import lru_cache
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tests/support/abc'),str(ROOT/'tests/support/isa')]
from builder import Module,FunctionCode,FunctionKind,Instruction,Profile,build,BuildError
from oracle_evidence import sha,record_node
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/native_frame'
SDK=Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony')
COMPILER=SDK/'ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc';DISASM=SDK/'toolchains/ark_disasm'
@lru_cache(maxsize=1)
def catalog():
    names={int(v):n for n,v in re.findall(r'\bk(\w+)\s*=\s*(\d+)',(BUILD/'generated/abcd/abc/generated_isa.h').read_text())}
    return {names[r['opcode']]:r for line in subprocess.check_output([BUILD/'isa_coverage_probe','--catalog'],text=True).splitlines() if (r:=json.loads(line))}
def encode(s,values=()):
    bits=shift=0
    assert len(values)==len(s['format'])
    for value,f in zip(values,s['format']):
        assert 0<=value<1<<f['width'];bits|=value<<shift;shift+=f['width']
    return Instruction(s['opcode'],bits.to_bytes((shift+7)//8,'little'),opcode_width=2 if s['prefix'] is not None else 1)
def specimen(kind,profile,count=1):
    c=catalog();op=lambda name,args=():encode(c[name],args)
    if 'THISBY' in kind:
        if kind.startswith('LDTHISBYNAME'): body=(op(kind,(0,2)),op('RETURN'))
        elif kind.startswith('LDTHISBYVALUE'): body=(op('LDA_V8',(4,)),op(kind,(0,)),op('RETURN'))
        elif kind.startswith('STTHISBYNAME'): body=(op('LDAI_IMM32',(1,)),op(kind,(0,2)),op('LDTHIS'),op('RETURN'))
        else: body=(op('LDAI_IMM32',(1,)),op(kind,(0,4)),op('LDTHIS'),op('RETURN'))
        child=FunctionCode('f',body,registers=1,arguments=4 if 'BYVALUE' in kind else 3)
    elif kind in ('STOWNBYNAME_IMM16_ID16_V8','STOWNBYVALUE_IMM16_V8_V8'):
        body=[op('CREATEEMPTYOBJECT'),op('STA_V8',(0,))]
        if 'BYVALUE' in kind:body += [op('LDA_STR_ID16',(2,)),op('STA_V8',(1,))]
        body += [op('LDAI_IMM32',(1,)),op(kind,(0,2,0) if 'BYNAME' in kind else (0,0,1)),op('LDA_V8',(0,)),op('RETURN')]
        child=FunctionCode('f',tuple(body),registers=2)
    elif kind=='WIDE_STOWNBYINDEX_PREF_V8_IMM32':
        child=FunctionCode('f',(op('CREATEEMPTYARRAY_IMM8',(0,)),op('STA_V8',(0,)),op('LDAI_IMM32',(1,)),op(kind,(0,0)),op('LDA_V8',(0,)),op('RETURN')))
    elif kind=='CREATEITERRESULTOBJ_V8_V8':
        child=FunctionCode('f',(op('LDAI_IMM32',(7,)),op('STA_V8',(0,)),op('LDTRUE'),op('STA_V8',(1,)),op(kind,(0,1)),op('RETURN')),registers=2)
    elif kind=='WIDE_COPYRESTARGS_PREF_IMM16':
        child=FunctionCode('f',(op(kind,(1,)),op('RETURN')),arguments=5)
    elif kind=='THROW_UNDEFINEDIFHOLE_PREF_V8_V8':
        child=FunctionCode('f',(op('LDAI_IMM32',(1,)),op('STA_V8',(0,)),op('LDA_STR_ID16',(2,)),op('STA_V8',(1,)),op(kind,(0,1)),op('LDA_V8',(0,)),op('RETURN')),registers=2)
    else: child=FunctionCode('f',tuple(op(kind) for _ in range(count))+(op('RETURN'),))
    return Module((op('DEFINEFUNC_IMM8_ID16_IMM8',(0,1,1 if ('THISBYVALUE' in kind or kind=='WIDE_COPYRESTARGS_PREF_IMM16') else 0)),op('STMODULEVAR_IMM8',(0,)),op('RETURNUNDEFINED')),
        profile=profile,functions=(child,),exports=('f',),pool_strings=('p',) if ('THISBY' in kind or kind.startswith(('STOWNBY','THROW_UNDEFINEDIFHOLE'))) else ())
def probe(abc):
    return [r for line in subprocess.check_output([BUILD/'isa_coverage_probe',abc],text=True,stderr=subprocess.DEVNULL).splitlines() if (r:=json.loads(line))['type']=='instruction']
def generate():
    entries=[]
    for kind,body in [('LDNEWTARGET','return new.target;'),('LDTHIS','return this;'),('LDFUNCTION','return f;'),('STOWNBYNAME_IMM16_ID16_V8','return {p:1};'),('WIDE_STOWNBYINDEX_PREF_V8_IMM32','return [1];'),('CREATEITERRESULTOBJ_V8_V8','return {value:7,done:true};'),('WIDE_COPYRESTARGS_PREF_IMM16','return rest;')]+[(n,'this.p=1;return this;' if n.startswith('ST') else 'return this.p;') for n in catalog() if n.startswith(('LDTHISBY','STTHISBY'))]:
        src=FIX/(kind.lower()+'.js');src.write_text('export function f('+('k' if 'THISBYVALUE' in kind else 'a,...rest' if kind=='WIDE_COPYRESTARGS_PREF_IMM16' else '')+'){'+(body.replace('this.p','this[k]') if 'THISBYVALUE' in kind else body)+'}\n')
        for profile in [Profile.DYNAMIC1206,Profile.DYNAMIC2400]:
            path=FIX/(kind.lower()+'-'+profile.name.lower()+'.abc');path.write_bytes(build(specimen(kind,profile))[0])
            entries.append({'path':path.name,'sha256':sha(path),'kind':kind,'profile':profile.name,'source':src.name,'source_sha256':sha(src),'origin':'constructed','opcodes':[catalog()[kind]['opcode']]})
    source=FIX/'newtarget-sdk.js';source.write_text('export function target(){return new.target;}\nexport class A{constructor(){this.target=new.target;}}\n')
    for mode,opts in [('default',[]),('o0',['--opt-level=0'])]:
        abc=FIX/('newtarget-sdk-'+mode+'.abc');cmd=[str(COMPILER),'--module','--merge-abc','--extension=js','--record-name=isaFrame','--source-file='+source.name,*opts,'--output='+str(abc.relative_to(ROOT)),str(source.relative_to(ROOT))]
        subprocess.run(cmd,check=True,cwd=ROOT)
        entries.append({'path':abc.name,'sha256':sha(abc),'source':source.name,'source_sha256':sha(source),'kind':'sdk','origin':'es2abc','command':cmd,'opcodes':sorted({r['encoding']['opcode'] for r in probe(abc)})})
    (FIX/'manifest.json').write_text(json.dumps({'compiler_sha256':sha(COMPILER),'disassembler_sha256':sha(DISASM),'fixtures':entries},indent=2)+'\n')
    p=ROOT/'tests/isa/coverage_manifest.json';d=json.loads(p.read_text());prefix='tests/fixtures/isa/native_frame/'
    d['fixtures']=[e for e in d['fixtures'] if not e['path'].startswith(prefix)]
    for e in entries:d['fixtures'].append({'path':prefix+e['path'],'sha256':e['sha256'],'opcodes':e['opcodes'],'classification':'Unmeasured','oracles':{},'note':'Only executed occurrence evidence can promote a fixture. new.target requires V7.'})
    if not any(x['test']=='isa_native_frame_test' for x in d['oracle_producers']):d['oracle_producers'].append({'test':'isa_native_frame_test','producer':'tests/isa/native_frame_test.py'})
    p.write_text(json.dumps(d,indent=2)+'\n')
class NativeFrame(unittest.TestCase):
    def test_complete_oracles(self):
        data=json.loads((FIX/'manifest.json').read_text());self.assertEqual(sha(COMPILER),data['compiler_sha256']);self.assertEqual(sha(DISASM),data['disassembler_sha256'])
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for i,e in enumerate(data['fixtures']):
                with self.subTest(fixture=e['path']):
                    abc=FIX/e['path'];source=FIX/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
                    if e['origin']=='constructed':self.assertEqual(abc.read_bytes(),build(specimen(e['kind'],Profile[e['profile']]))[0])
                    else:
                        cmd=[('--output='+str(root/'compiled.abc')) if a.startswith('--output=') else a for a in e['command']]
                        subprocess.run(cmd,check=True,cwd=ROOT);self.assertEqual(sha(root/'compiled.abc'),e['sha256'])
                    subprocess.run([DISASM,abc,root/'out.pa'],check=True,capture_output=True)
                    out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True)
                    record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2)
                    self.assertEqual(record['source_status'],0,record)
                    emitted=out/record['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
                    result=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/isa_native_frame.mjs',source,emitted,e['kind']],text=True))
                    record_node(abc,out,source,__file__,result['cases'],BUILD/'abc_structure',exclusions=['LDNEWTARGET: interpreter fatal/no-op behavior excluded by V7; source follows AOT'] if e['kind']=='LDNEWTARGET' else [])
    def test_refused_premise_and_owner(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for i,(kind,contract,arrow) in enumerate([
                ('LDNEWTARGET',False,False),('unused-new-target',False,False),('LDNEWTARGET',True,True),
                ('LDTHISBYNAME_IMM8_ID16',True,True),('STTHISBYNAME_IMM8_ID16',True,True)]):
                module=specimen('LDNEWTARGET' if kind=='unused-new-target' else kind,Profile.DYNAMIC1206)
                if kind=='unused-new-target':module=replace(module,functions=(replace(module.functions[0],instructions=(encode(catalog()['LDNEWTARGET']),encode(catalog()['RETURNUNDEFINED']))),))
                if arrow: module=replace(module,functions=(replace(module.functions[0],kind=FunctionKind.ARROW),))
                abc=root/(str(i)+'.abc');abc.write_bytes(build(module)[0]);out=root/str(i)
                subprocess.run([BUILD/'abc_structure',abc,out,'--value-source',*(['--language-source'] if contract else ['--language-numeric','--language-constructors'])],check=True,capture_output=True)
                record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2)
                self.assertNotEqual(record['source_status'],0,record)
                self.assertTrue(record.get('source_failure') or record.get('source_omitted_members'),record)
    def test_builder_budgets_and_decode(self):
        for n in [200,201,400,800]:
            m=specimen('LDNEWTARGET',Profile.DYNAMIC1206,n);expected=build(m)[0];lo,hi=0,1024*1024
            while lo<hi:
                mid=(lo+hi)//2
                try:build(m,mid);hi=mid
                except BuildError:lo=mid+1
            self.assertEqual(build(m,lo)[0],expected)
            with self.assertRaises(BuildError):build(m,lo-1)
            with tempfile.TemporaryDirectory() as d:
                abc=Path(d)/'scale.abc';abc.write_bytes(expected);hits=[r for r in probe(abc) if r['encoding']['opcode']==catalog()['LDNEWTARGET']['opcode']]
                self.assertEqual(len(hits),n);self.assertTrue(all(r['lift']['status']=='ok' for r in hits))
        with tempfile.TemporaryDirectory() as d:
            index=Path(d)/'index.json';subprocess.run([BUILD/'abc_scan',index,'frame',FIX],check=True,capture_output=True)
            data=json.loads(subprocess.check_output([BUILD/'abc_decode',index,'frame',FIX],text=True));self.assertEqual(data['failed'],0);self.assertEqual(data['failed_methods'],0)
if __name__=='__main__':
    if '--generate' in sys.argv:generate()
    else:unittest.main()
