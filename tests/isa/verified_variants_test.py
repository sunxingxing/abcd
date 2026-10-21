#!/usr/bin/env python3
"""Complete occurrence oracles, distinct from the earlier raw-only ISA probes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tests/support/abc'))
sys.path.insert(0, str(ROOT/'tests/support/isa'))
from builder import Instruction, Module, Profile, build, BuildError
from oracle_evidence import record_node
BUILD = ROOT/'build/regression'
FIX = ROOT/'tests/fixtures/isa/verified'
ES2ABC = Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc')
DISASM = Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains/ark_disasm')

def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()

def catalog():
    import re
    names={int(v):n for n,v in re.findall(r'\bk(\w+)\s*=\s*(\d+)',(BUILD/'generated/abcd/abc/generated_isa.h').read_text())}
    return {names[r['opcode']]:r for line in subprocess.check_output([BUILD/'isa_coverage_probe','--catalog'],text=True).splitlines() if (r:=json.loads(line))}

def encode(spec, operands=()):
    assert len(operands)==len(spec['format'])
    bits=shift=0
    for value,field in zip(operands,spec['format']):
        assert 0<=value<1<<field['width']
        bits|=value<<shift;shift+=field['width']
    return Instruction(spec['opcode'],bits.to_bytes((shift+7)//8,'little'),opcode_width=2 if spec['prefix'] is not None else 1)

def indexed(spec, profile, index=4294967295, count=1):
    c=catalog()
    # Real initialized input and ordinary strict [[Get]], not arbitrary raw operands.
    old=spec['name'].startswith('DEPRECATED_')
    store='STOBJBYINDEX' in spec['name']
    wide=spec['name'].startswith('WIDE_')
    index=min(index,(1<<spec['format'][-1]['width'])-1)
    load=encode(c['TRYLDGLOBALBYNAME_IMM8_ID16'],(0,1))
    prefix=(load,encode(c['STA_V8'],(0,))) if old or store else ()
    target=encode(spec,(0,0,index) if store and not wide else (0,index) if not wide or store else (index,))
    if store:
        return Module(prefix+(encode(c['LDAI_IMM32'],(17,)),target)*count+(encode(c['RETURNUNDEFINED']),),registers=1,pool_strings=('input',),profile=profile),index
    return Module(prefix+tuple(i for _ in range(count) for i in ((encode(c['LDA_V8'],(0,)),target) if old else (load,target)))+
        (encode(c['RETURNUNDEFINED']),),registers=1,pool_strings=('input',),profile=profile),index

def generate():
    c=catalog(); entries=[]
    for source_name in ('expressions', 'wide-new', 'wide-super'):
      source=FIX/(source_name+'.js')
      for mode,options in [('default',[]),('o0',['--opt-level=0'])]:
        path=FIX/f'{source_name}-{mode}.abc'
        command=[str(ES2ABC),'--module','--merge-abc','--record-name=isaVerified',
                 '--source-file='+source.name,*options,'--output='+str(path.relative_to(ROOT)),str(source.relative_to(ROOT))]
        subprocess.run(command,cwd=ROOT,check=True)
        entries.append({'path':path.name,'sha256':sha(path),'source':source.name,
                        'source_sha256':sha(source),'command':command,'origin':'es2abc'})
    for name in ['LDOBJBYINDEX_IMM8_IMM16','LDOBJBYINDEX_IMM16_IMM16','WIDE_LDOBJBYINDEX_PREF_IMM32','DEPRECATED_LDOBJBYINDEX_PREF_V8_IMM32','STOBJBYINDEX_IMM8_V8_IMM16','STOBJBYINDEX_IMM16_V8_IMM16','WIDE_STOBJBYINDEX_PREF_V8_IMM32']:
        spec=dict(c[name],name=name)
        for profile in [Profile.DYNAMIC1206,Profile.DYNAMIC2400]:
            module,index=indexed(spec,profile);path=FIX/f'{name.lower()}-{profile.name.lower()}.abc';path.write_bytes(build(module)[0])
            source=path.with_suffix('.js');source.write_text(f'input[{index}]'+(' = 17' if 'STOBJBYINDEX' in name else '')+';\n')
            entries.append({'path':path.name,'sha256':sha(path),'source':source.name,'source_sha256':sha(source),'origin':'constructed','variant':name,'profile':profile.name,'opcodes':[spec['opcode']]})
    manifest={'compiler':str(ES2ABC),'compiler_sha256':sha(ES2ABC),'disassembler':str(DISASM),'disassembler_sha256':sha(DISASM),'fixtures':entries}
    (FIX/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    p=ROOT/'tests/isa/coverage_manifest.json';registry=json.loads(p.read_text());registry['fixtures']=[e for e in registry['fixtures'] if not e['path'].startswith('tests/fixtures/isa/verified/')]
    for e in entries:
        registry['fixtures'].append({'path':'tests/fixtures/isa/verified/'+e['path'],'sha256':e['sha256'],'opcodes':e.get('opcodes',[]),'classification':'Unmeasured','oracles':{},'note':'Classification comes from executed occurrence evidence, never this registration.'})
    p.write_text(json.dumps(registry,indent=2)+'\n')

class Verified(unittest.TestCase):
    def test_complete_differentials(self):
        c=catalog(); manifest=json.loads((FIX/'manifest.json').read_text())
        self.assertEqual(sha(ES2ABC),manifest['compiler_sha256']);self.assertEqual(sha(DISASM),manifest['disassembler_sha256'])
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for n,e in enumerate(manifest['fixtures']):
                with self.subTest(fixture=e['path']):
                    abc=FIX/e['path'];source=FIX/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
                    if e['origin']=='es2abc':
                        command=[('--output='+str(root/'compiler.abc')) if a.startswith('--output=') else a for a in e['command']]
                        subprocess.run(command,cwd=ROOT,check=True);self.assertEqual(sha(root/'compiler.abc'),e['sha256'])
                    else:
                        module,_=indexed(dict(c[e['variant']],name=e['variant']),Profile[e['profile']]);self.assertEqual(build(module)[0],abc.read_bytes())
                    subprocess.run([DISASM,abc,root/'out.pa'],check=True,capture_output=True)
                    out=root/str(n);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True)
                    records=[r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2]
                    self.assertEqual(len(records),1);self.assertEqual(records[0]['source_status'],0,records[0].get('source_failure'))
                    emitted=out/records[0]['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
                    result=subprocess.check_output(['node',ROOT/'tests/integration/isa_verified.mjs',source,emitted,e['origin']],text=True)
                    cases=json.loads(result)['cases'];record_node(abc,out,source,__file__,cases,BUILD/'abc_structure')

    def test_corpus_decoder(self):
        with tempfile.TemporaryDirectory() as tmp:
            index=Path(tmp)/'index.json'
            subprocess.run([BUILD/'abc_scan',index,'verified',FIX],check=True,capture_output=True)
            result=subprocess.check_output([BUILD/'abc_decode',index,'verified',FIX],text=True)
            report=json.loads(result)
            self.assertEqual(report['failed_methods'],0);self.assertEqual(report['failed'],0)
            self.assertTrue(all(r['outcome']=='decoded' for r in report['results']))

    def test_scale_exact_budgets(self):
        spec=dict(catalog()['WIDE_LDOBJBYINDEX_PREF_IMM32'],name='WIDE_LDOBJBYINDEX_PREF_IMM32')
        for n in (200,201,400,800):
            module,_=indexed(spec,Profile.DYNAMIC1206,count=n);expected=build(module)[0];lo,hi=0,1024*1024
            while lo<hi:
                mid=(lo+hi)//2
                try:build(module,mid);hi=mid
                except BuildError:lo=mid+1
            self.assertEqual(build(module,lo)[0],expected)
            with self.assertRaises(BuildError):build(module,lo-1)
            with tempfile.TemporaryDirectory() as tmp:
                abc=Path(tmp)/'scale.abc';abc.write_bytes(expected)
                rows=[json.loads(line) for line in subprocess.check_output([BUILD/'isa_coverage_probe',abc],text=True,stderr=subprocess.DEVNULL).splitlines()]
                hits=[r for r in rows if r['type']=='instruction' and r['encoding']['opcode']==spec['opcode']]
                self.assertEqual(len(hits),n);self.assertTrue(all(r['lift']['status']=='ok' for r in hits))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--generate',action='store_true');args=parser.parse_args()
    if args.generate:generate()
    else:unittest.main(argv=[sys.argv[0]])
