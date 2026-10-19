#!/usr/bin/env python3
"""Property ISA evidence. Decode/refusal is not source-semantic support."""
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
from builder import Instruction, Module, build, BuildError
BUILD = ROOT/'build/regression'
FIX = ROOT/'tests/fixtures/isa/properties'
SDK = Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony')
ES2ABC = SDK/'ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc'
DISASM = SDK/'toolchains/ark_disasm'
FAMILIES = ('LDTHISBYNAME', 'STTHISBYNAME', 'LDTHISBYVALUE', 'STTHISBYVALUE',
            'LDSUPERBYVALUE', 'STSUPERBYVALUE', 'STSUPERBYNAME', 'STOBJBYINDEX',
            'WIDE_STOBJBYINDEX', 'STOWNBYNAMEWITHNAMESET', 'STOWNBYVALUEWITHNAMESET')

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def catalog():
    names = {int(v): n for n,v in re.findall(r'\bk(\w+)\s*=\s*(\d+)',
        (BUILD/'generated/abcd/abc/generated_isa.h').read_text())}
    return {names[r['opcode']]: r for line in subprocess.check_output(
        [BUILD/'isa_coverage_probe','--catalog'],text=True).splitlines() if (r:=json.loads(line))}

def selected():
    return {n:s for n,s in catalog().items() if any(n.startswith(f+'_') for f in FAMILIES)}

def specimen(spec, count=1):
    # The generated format supplies operand widths and kinds. String slot 1
    # follows the method reference in the builder's regional method index.
    operands=b''.join((1 if f['kind']==5 else 0).to_bytes(f['width']//8,'little') for f in spec['format'])
    instruction=Instruction(spec['opcode'], operands, opcode_width=2 if spec['prefix'] is not None else 1)
    c=catalog()
    return Module((Instruction(c['LDUNDEFINED']['opcode']),)+(instruction,)*count+
        (Instruction(c['RETURNUNDEFINED']['opcode']),),pool_strings=('p',))

def probe(path):
    return [r for line in subprocess.check_output([BUILD/'isa_coverage_probe',path],text=True,stderr=subprocess.DEVNULL).splitlines()
            if (r:=json.loads(line))['type']=='instruction']

def source():
    # Enough distinct IC sites to exercise both cache widths. These are inputs
    # to the frontend, never opcode-sequence admission in the decompiler.
    lines=['export class Base { get p() { return this.x; } set p(v) { this.x=v; } }',
      'export class Derived extends Base {', 'read(k) {']
    lines += ['super[k];']*140
    lines += ['return this.p + this[k]; }', 'write(k,v) {']
    lines += ['super.p=v; super[k]=v;']*140
    lines += ['this.p=v; this[k]=v; return v; }', '}',
      'export function index(o,v) { o[0]=v; o[65535]=v; o[65536]=v; o[2147483648]=v; o[4294967295]=v; return v; }',
      'export function smallNames(k) { return { [k]:function(){}, \"a.\\\\b\":function(){} }; }',
      'export function names(k) { return { [k]:function(){},']
    lines += [f"['k{i}']:function(){{}}, 'name.{i}\\\\b':function(){{}}," for i in range(140)]
    lines += ['}; }']
    return '\n'.join(lines)+'\n'

def generate():
    FIX.mkdir(parents=True,exist_ok=True)
    (FIX/'properties.js').write_text(source())
    entries=[]
    for name,spec in selected().items():
        path=FIX/(name.lower()+'.abc'); path.write_bytes(build(specimen(spec))[0])
        entries.append({'name':name,'opcode':spec['opcode'],'path':path.name,'sha256':sha(path),
            'origin':'constructed','oracle':None})
    for label,options in [('o0',['--opt-level=0']),('default',[])]:
        path=FIX/(label+'.abc')
        subprocess.run([ES2ABC,'--module','--merge-abc',*options,'--output='+str(path),FIX/'properties.js'],check=True)
        present={r['encoding']['opcode'] for r in probe(path)}
        entries.append({'path':path.name,'sha256':sha(path),'origin':'es2abc','options':options,
            'variants':[n for n,s in selected().items() if s['opcode'] in present], 'oracle':None})
    (FIX/'manifest.json').write_text(json.dumps({'schema':1,'compiler_sha256':sha(ES2ABC),
        'source_sha256':sha(FIX/'properties.js'),'fixtures':entries},indent=2)+'\n')
    registry_path=ROOT/'tests/isa/coverage_manifest.json'
    registry=json.loads(registry_path.read_text())
    prefix='tests/fixtures/isa/properties/'
    registry['fixtures']=[e for e in registry['fixtures'] if not e['path'].startswith(prefix)]
    for entry in entries:
        registry['fixtures'].append({'path':prefix+entry['path'],'sha256':entry['sha256'],
            'profile':4,'opcodes':[entry['opcode']] if entry['origin']=='constructed' else [],
            'classification':'Unmeasured','oracles':{},
            'note':'Implemented pinned handler; decode/emission evidence only, source semantics not verified.'})
    registry_path.write_text(json.dumps(registry,indent=2)+'\n')

class Properties(unittest.TestCase):
    def test_catalog_and_runtime_handlers(self):
        variants=selected(); self.assertEqual(len(variants),21)
        runtime=(ROOT/'references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp').read_text()
        for name in variants:
            start=runtime.index('HANDLE_OPCODE('+name+')')
            end=runtime.find('HANDLE_OPCODE(',start+1)
            handler=runtime[start:end]
            self.assertNotIn('LOG_FULL(FATAL)',handler)
            self.assertIn('INTERPRETER_RETURN_IF_ABRUPT',handler)
            self.assertIn('DISPATCH('+name+')',handler)

    def test_constructed_decode_and_current_gap(self):
        manifest=json.loads((FIX/'manifest.json').read_text())
        self.assertEqual(sha(FIX/'properties.js'),manifest['source_sha256'])
        self.assertEqual((FIX/'properties.js').read_text(),source())
        self.assertEqual(sha(ES2ABC),manifest['compiler_sha256'])
        with tempfile.TemporaryDirectory() as tmp:
            for entry in manifest['fixtures']:
                path=FIX/entry['path']; self.assertEqual(sha(path),entry['sha256'])
                subprocess.run([DISASM,path,Path(tmp)/(path.stem+'.pa')],check=True,capture_output=True)
                rows=probe(path)
                if entry['origin']=='constructed':
                    spec=selected()[entry['name']]
                    self.assertEqual(path.read_bytes(),build(specimen(spec))[0])
                    self.assertEqual(rows[1]['encoding']['opcode'],entry['opcode'])
                    self.assertEqual(rows[1]['encoding']['format'],spec['format'])
                    # Raw naming semantics are now lifted; this is still not
                    # a claim that source recovery or an oracle passed.
                    if 'WITHNAMESET' in entry['name']:
                        self.assertEqual(rows[1]['lift']['status'],'ok')
                        self.assertEqual(rows[1]['lift']['roles']['name_setting'],1)
                        self.assertIsNone(rows[1]['lift']['normal_commit'])
                        self.assertEqual(rows[1]['lift']['exceptional_completion'],1)
                    else:
                        self.assertEqual(rows[1]['lift']['status'],'refused')
                        self.assertEqual(rows[1]['lift']['cause']['value'],1)
                    self.assertNotIn('non_executable',rows[1]['lift'])
                else:
                    regenerated=Path(tmp)/path.name
                    subprocess.run([ES2ABC,'--module','--merge-abc',*entry['options'],
                        '--output='+str(regenerated),FIX/'properties.js'],check=True,capture_output=True)
                    self.assertEqual(sha(regenerated),entry['sha256'])
                    present={r['encoding']['opcode'] for r in rows}
                    self.assertEqual(entry['variants'],[n for n,s in selected().items() if s['opcode'] in present])

    def test_scale_and_exact_builder_budget(self):
        spec=selected()['WIDE_STOBJBYINDEX_PREF_V8_IMM32']
        for n in (200,201,400,800):
            module=specimen(spec,n); expected=build(module)[0]
            lo,hi=0,1024*1024
            while lo<hi:
                mid=(lo+hi)//2
                try: build(module,mid); hi=mid
                except BuildError: lo=mid+1
            self.assertEqual(build(module,lo)[0],expected)
            with self.assertRaises(BuildError): build(module,lo-1)
            with tempfile.TemporaryDirectory() as tmp:
                path=Path(tmp)/'scale.abc';path.write_bytes(expected)
                rows=probe(path)
                self.assertEqual(len(rows),n+2)
                self.assertTrue(all(r['lift']['cause']['value']==1 for r in rows[1:-1]))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--generate',action='store_true')
    args=parser.parse_args()
    if args.generate: generate()
    else: unittest.main(argv=[sys.argv[0]])
