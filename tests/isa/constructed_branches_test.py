#!/usr/bin/env python3
"""Constructed-ABC refusal evidence; never execute a non-executable opcode."""
import argparse
import dataclasses
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
import zlib

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/support/abc'))
from builder import BuildError, Catch, Instruction, Label, Module, build

SDK = Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony')
DISASM = SDK / 'toolchains/ark_disasm'
ES2ABC = SDK / 'ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc'
FIXTURES = ROOT / 'tests/fixtures/isa/branches'
BUILD = ROOT / 'build/regression'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def catalog():
    source = (BUILD / 'generated/abcd/abc/generated_isa.h').read_text()
    return {name: int(value) for name, value in re.findall(r'\bk(\w+)\s*=\s*(\d+)', source)}

def variants():
    # The typed production evidence table is the authority, not a name pattern.
    source = (ROOT / 'include/abcd/analysis/non_executable.h').read_text()
    names = re.findall(r'abc::Opcode::k(\w+)', source)
    assert len(names) == len(set(names)) == 28
    return names

def specimen(name):
    op = catalog()[name]
    spec = next(json.loads(line) for line in subprocess.check_output(
        [BUILD / 'isa_coverage_probe', '--catalog'], text=True).splitlines()
        if json.loads(line)['opcode'] == op)
    # Branch delta is the last immediate in the real generated format.
    width = spec['format'][-1]['width'] // 8
    operands = bytes((spec['size'] - 1 - width))
    return Module((Instruction(catalog()['LDUNDEFINED']),
                   Instruction(op, operands, Label(0), width),
                   Label(0), Instruction(catalog()['RETURNUNDEFINED'])))

def positive(name):
    c = catalog()
    return Module((Instruction(c['TRYLDGLOBALBYNAME_IMM8_ID16'], bytes((0, 1, 0))),
        Instruction(c['ISTRUE']), Instruction(c[name], target=Label(0), delta_width=1),
        Instruction(c['TRYLDGLOBALBYNAME_IMM8_ID16'], bytes((0, 2, 0))),
        Label(0), Instruction(c['RETURNUNDEFINED'])), pool_strings=('input', 'observed'))

def generate():
    FIXTURES.mkdir(parents=True, exist_ok=True)
    registry = []
    for name in variants():
        data, _ = build(specimen(name))
        path = FIXTURES / (name.lower() + '.abc')
        path.write_bytes(data)
        registry.append({'path': str(path.relative_to(ROOT)), 'sha256': digest(path),
            'profile': 4, 'opcodes': [catalog()[name]], 'classification': 'NonExecutable',
            'evidence_id': 0,
            'evidence': 'docs/design/source-recovery/isa-branches-runtime-evidence.md',
            'evidence_sha256': digest(ROOT/'docs/design/source-recovery/isa-branches-runtime-evidence.md'),
            'refusal_test': 'isa_constructed_branches_test', 'oracles': {}})
    for name in ('JEQZ_IMM8', 'JNEZ_IMM8'):
        (FIXTURES / (name.lower() + '.abc')).write_bytes(build(positive(name))[0])
        js = FIXTURES / (name.lower() + '.js')
        js.write_text('if (' + ('' if name.startswith('JEQZ') else '!') + 'input) { observed; }\n')
        subprocess.run([ES2ABC, '--module', '--merge-abc', '--opt-level=0',
            '--output=' + str(FIXTURES / (name.lower() + '-es2abc.abc')), js], check=True)
    document = json.loads((ROOT / 'tests/isa/coverage_manifest.json').read_text())
    registry.extend(e for e in document['fixtures'] if not e['path'].startswith('tests/fixtures/isa/branches/'))
    document['fixtures'] = registry
    # Oracle producers and other independent fixture families survive regeneration.
    (ROOT / 'tests/isa/coverage_manifest.json').write_text(json.dumps(document, indent=2)+'\n')

class BuilderTest(unittest.TestCase):
    def test_runtime_evidence(self):
        # This checks the reviewed citation, not coverage or executable support.
        runtime=(ROOT/'references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp').read_text()
        for name in variants():
            handler=re.search(r'HANDLE_OPCODE\('+name+r'\)\s*\{([^}]+)\}',runtime)
            self.assertIsNotNone(handler)
            self.assertIn('LOG_FULL(FATAL) << "not implement"',handler.group(1))
            self.assertNotIn('GET_ACC',handler.group(1))

    def test_reproducible_and_budgets(self):
        for n in (200, 201, 400, 800):
            # Both forward and backward label fixups; no execution claimed.
            m = Module((Label(0),) + (Instruction(catalog()['LDUNDEFINED']),) * n +
                       (Instruction(catalog()['JEQZ_IMM16'], target=Label(0), delta_width=2),
                        Instruction(catalog()['RETURNUNDEFINED'])))
            expected, offsets = build(m)
            lo, hi = 0, 1024*1024
            while lo < hi:
                mid = (lo+hi)//2
                try: build(m, mid); hi = mid
                except BuildError: lo = mid+1
            self.assertEqual(build(m, lo)[0], expected)
            with self.assertRaises(BuildError): build(m, lo-1)
            self.assertEqual(int.from_bytes(expected[16:20], 'little'), len(expected))
            self.assertEqual(int.from_bytes(expected[8:12], 'little'), zlib.adler32(expected[12:]) & ((1<<32)-1))
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp)/'scale.abc'; path.write_bytes(expected)
                subprocess.run([DISASM, path, Path(tmp)/'scale.pa'], check=True, capture_output=True)
                rows = self.probe(path)
                self.assertEqual(len(rows), n+2)
                self.assertEqual(rows[-2]['branch_target'], 0)

    def probe(self, path):
        return [r for line in subprocess.check_output([BUILD/'isa_coverage_probe',path],text=True).splitlines()
                if (r := json.loads(line))['type'] == 'instruction']

    def recover(self, path, dest):
        subprocess.run([BUILD/'abc_structure',path,dest,'--value-source','--language-source'],check=True,capture_output=True)
        return next(r for r in json.loads((dest/'manifest.json').read_text())['records'] if r['source_status'] != 2)

    def test_all_refusals_and_sdk(self):
        registry = json.loads((ROOT/'tests/isa/coverage_manifest.json').read_text())['fixtures']
        registry = [e for e in registry if e['path'].startswith('tests/fixtures/isa/branches/')]
        self.assertTrue(all(e.get('classification') == 'NonExecutable' for e in registry))
        self.assertEqual(len(registry),28)
        with tempfile.TemporaryDirectory() as tmp:
            for n, name in enumerate(variants()):
                path = FIXTURES/(name.lower()+'.abc')
                data, offsets = build(specimen(name))
                self.assertEqual(data,path.read_bytes())
                self.assertEqual(digest(path), registry[n]['sha256'])
                asm = Path(tmp)/(name+'.pa')
                subprocess.run([DISASM,path,asm],check=True,capture_output=True)
                self.assertIn(name.split('_')[0].lower()+' ',asm.read_text())
                rows = self.probe(path)
                row = rows[1]
                self.assertEqual(row['encoding']['opcode'],catalog()[name])
                self.assertEqual(row['pc'],1)
                self.assertEqual(row['lift']['status'],'refused')
                self.assertEqual(row['lift']['cause']['value'],7)
                evidence = {'opcode':catalog()[name],'evidence':0}
                self.assertEqual(row['lift']['non_executable'],evidence)
                record = self.recover(path,Path(tmp)/name)
                self.assertFalse(record['source_complete'])
                self.assertEqual(record['source_failure']['non_executable'],evidence)
                self.assertEqual(record['source_failure']['causes'][-1],
                    {'domain':'abcd::analysis::SemanticError','value':7})
                cli = subprocess.run([BUILD/'abc_semantic',path,str(offsets['METHOD']),'--record-source'],capture_output=True,text=True)
                described = json.loads(cli.stdout)
                self.assertEqual(described['source_failure']['non_executable'],evidence)

    def test_optional_catch_and_invalid_layouts(self):
        c=catalog()
        m=Module((Label(0),Instruction(c['LDUNDEFINED']),Instruction(c['THROW_PREF_NONE'],opcode_width=2),
            Label(1),Instruction(c['RETURNUNDEFINED']),Label(2)),
            catches=(Catch(Label(0),Label(1),Label(1),Label(2)),))
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'catch.abc'; path.write_bytes(build(m)[0])
            asm=Path(tmp)/'catch.pa'
            subprocess.run([DISASM,path,asm],check=True,capture_output=True)
            self.assertIn('.catchall',asm.read_text())
            rows=self.probe(path)
            self.assertEqual(rows[1]['lift']['exception_exit']['handler_pc'],3)
        bad=[Module((Label(-1),)),Module((Label(1<<32),)),Module((Label(0),Label(0))),Module((Instruction(0x4f,target=Label(0),delta_width=1),Label(0))),Module((Instruction(0x4f,target=Label(5),delta_width=1),)),
             Module((Instruction(0x4f,target=Label(0),delta_width=1),)+(Instruction(0),)*200+(Label(0),)),
             dataclasses.replace(m,registers=-1),dataclasses.replace(m,arguments=1<<32),dataclasses.replace(m,name='bad\0name')]
        for module in bad:
            with self.assertRaises(BuildError): build(module)
        with self.assertRaises(BuildError): build(m, 128*1024*1024+1)

    def test_positive_controls_node(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for name in ('JEQZ_IMM8','JNEZ_IMM8'):
                paths=[]
                for suffix in ('','-es2abc'):
                    path=FIXTURES/(name.lower()+suffix+'.abc')
                    out=root/(name+suffix)
                    record=self.recover(path,out)
                    self.assertTrue(record['source_complete'],record)
                    paths.append(out/record['source'])
                paths.append(FIXTURES/(name.lower()+'.js'))
                # Same module text is evaluated in a fresh vm context per value.
                # Global getters make both evaluation count and taken edge observable.
                subprocess.run(['node',ROOT/'tests/isa/branch_oracle.mjs',*paths],check=True)

if __name__=='__main__':
    if '--generate' in sys.argv:
        generate()
    else:
        unittest.main()
