#!/usr/bin/env python3
"""Deprecated (PREF 0xfc) instruction forms, built with the test-only ABC
writer because es2abc no longer emits them. Each module exports `f`, whose
body applies one deprecated instruction to its parameter registers and returns
the accumulator. The oracle is ordinary JavaScript with the pinned
interpreter's semantics for that form; deprecated_source.mjs compares both
over a per-family input set."""
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
from builder import FunctionCode, Instruction, Module, Profile, build
from oracle_evidence import record_node
BUILD = ROOT/'build/regression'
FIX = ROOT/'tests/fixtures/isa/deprecated_source'
HARNESS = ROOT/'tests/integration/deprecated_source.mjs'
# Bodies use one local register unless stated, so parameters start at v4
# (after func, new.target, this). Each case: body instructions, parameter
# count, harness family, equivalent source, local registers.
def single(name, operands, arity, family, body):
    return name, ([(name, operands)], arity, family, body, 1)
NAME = 2  # Regional slot of the first pool string ('p'), after both methods.
CASES = dict([
    single('DEPRECATED_INC_PREF_V8', (4,), 1, 'unary', 'return ++a;'),
    single('DEPRECATED_DEC_PREF_V8', (4,), 1, 'unary', 'return --a;'),
    single('DEPRECATED_NEG_PREF_V8', (4,), 1, 'unary', 'return -a;'),
    single('DEPRECATED_NOT_PREF_V8', (4,), 1, 'unary', 'return ~a;'),
    single('DEPRECATED_TONUMBER_PREF_V8', (4,), 1, 'unary', 'return +a;'),
    # Postfix increment yields ToNumeric of the old value.
    single('DEPRECATED_TONUMERIC_PREF_V8', (4,), 1, 'unary', 'return a++;'),
    # DEPRECATED_CALL_INITIALIZE: callee register, then argument registers.
    single('DEPRECATED_CALLARG0_PREF_V8', (4,), 1, 'call', 'return a();'),
    single('DEPRECATED_CALLARG1_PREF_V8_V8', (4, 5), 2, 'call', 'return a(b);'),
    single('DEPRECATED_CALLARGS2_PREF_V8_V8_V8', (4, 5, 6), 3, 'call', 'return a(b, c);'),
    single('DEPRECATED_CALLARGS3_PREF_V8_V8_V8_V8', (4, 5, 6, 7), 4, 'call', 'return a(b, c, d);'),
    # Count excludes the callee; arguments follow it.
    single('DEPRECATED_CALLRANGE_PREF_IMM16_V8', (2, 4), 3, 'call', 'return a(b, c);'),
    single('DEPRECATED_LDOBJBYNAME_PREF_ID32_V8', (NAME, 4), 1, 'named', 'return a.p;'),
    single('DEPRECATED_DELOBJPROP_PREF_V8_V8', (4, 5), 2, 'delete', 'return delete a[b];'),
    single('DEPRECATED_DYNAMICIMPORT_PREF_V8', (4,), 1, 'import', 'return import(a);'),
    ('DEPRECATED_COPYDATAPROPERTIES_PREF_V8_V8',
     ([('CREATEEMPTYOBJECT', ()), ('STA_V8', (0,)), ('DEPRECATED_COPYDATAPROPERTIES_PREF_V8_V8', (0, 4))],
      1, 'spread', 'return {...a};', 1)),
    # Count includes the receiver: callee v0, this v1, one argument v2.
    ('DEPRECATED_CALLTHISRANGE_PREF_IMM16_V8',
     ([('DEPRECATED_LDOBJBYNAME_PREF_ID32_V8', (NAME, 6)), ('STA_V8', (0,)), ('MOV_V8_V8', (1, 6)),
       ('MOV_V8_V8', (2, 7)), ('DEPRECATED_CALLTHISRANGE_PREF_IMM16_V8', (2, 0))], 2, 'method', 'return a.p(b);', 3)),
    # CallSpread(callee v0, this v1, private array v2), as APPLY; a is v7.
    ('DEPRECATED_CALLSPREAD_PREF_V8_V8_V8',
     ([('DEPRECATED_LDOBJBYNAME_PREF_ID32_V8', (NAME, 7)), ('STA_V8', (0,)), ('MOV_V8_V8', (1, 7)),
       ('CREATEEMPTYARRAY_IMM8', (0,)), ('STA_V8', (2,)), ('LDAI_IMM32', (0,)), ('STA_V8', (3,)), ('LDA_V8', (8,)),
       ('STARRAYSPREAD_V8_V8', (2, 3)), ('DEPRECATED_CALLSPREAD_PREF_V8_V8_V8', (0, 1, 2))], 2, 'spreadcall', 'return a.p(...b);', 4)),
])
PARAMETERS = 'abcd'

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def catalog():
    import re
    names = {int(v): n for n, v in re.findall(r'\bk(\w+)\s*=\s*(\d+)', (BUILD/'generated/abcd/abc/generated_isa.h').read_text())}
    return {names[r['opcode']]: r for line in subprocess.check_output([BUILD/'isa_coverage_probe', '--catalog'], text=True).splitlines() if (r := json.loads(line))}

def encode(spec, operands=()):
    assert len(operands) == len(spec['format'])
    bits = shift = 0
    for value, field in zip(operands, spec['format']):
        assert 0 <= value < 1 << field['width']
        bits |= value << shift; shift += field['width']
    return Instruction(spec['opcode'], bits.to_bytes((shift + 7)//8, 'little'), opcode_width=2 if spec['prefix'] is not None else 1)

def module(name, profile):
    c = catalog(); body, arity, _, _, registers = CASES[name]
    child = FunctionCode('f', tuple(encode(c[op], operands) for op, operands in body) + (encode(c['RETURN']),),
                         registers=registers, arguments=3 + arity)
    return Module((encode(c['DEFINEFUNC_IMM8_ID16_IMM8'], (0, 1, arity)), encode(c['STMODULEVAR_IMM8'], (0,)),
                   encode(c['RETURNUNDEFINED'])), profile=profile, functions=(child,), exports=('f',), pool_strings=('p',))

def source(name):
    _, arity, _, body, _ = CASES[name]
    return f'export function f({", ".join(PARAMETERS[:arity])}) {{\n  {body}\n}}\n'

def generate():
    c = catalog(); entries = []
    FIX.mkdir(parents=True, exist_ok=True)
    for name, (_, _, family, _, _) in CASES.items():
        text = FIX/f'{name.lower()}.js'; text.write_text(source(name))
        for profile in (Profile.DYNAMIC1206, Profile.DYNAMIC2400):
            path = FIX/f'{name.lower()}-{profile.name.lower()}.abc'
            path.write_bytes(build(module(name, profile))[0])
            entries.append({'path': path.name, 'sha256': sha(path), 'source': text.name, 'source_sha256': sha(text),
                            'origin': 'constructed', 'variant': name, 'family': family, 'profile': profile.name,
                            'opcodes': [c[name]['opcode']]})
    (FIX/'manifest.json').write_text(json.dumps({'fixtures': entries}, indent=1) + '\n')
    p = ROOT/'tests/isa/coverage_manifest.json'; registry = json.loads(p.read_text())
    prefix = 'tests/fixtures/isa/deprecated_source/'
    registry['fixtures'] = [e for e in registry['fixtures'] if not e['path'].startswith(prefix)]
    for e in entries:
        registry['fixtures'].append({'path': prefix + e['path'], 'sha256': e['sha256'],
            'opcodes': e['opcodes'], 'classification': 'Unmeasured', 'oracles': {},
            'note': 'Classification comes from executed occurrence evidence, never this registration.'})
    producer = {'test': 'isa_deprecated_source_test', 'producer': 'tests/isa/deprecated_source_test.py'}
    if producer not in registry['oracle_producers']: registry['oracle_producers'].append(producer)
    p.write_text(json.dumps(registry, indent=2) + '\n')

class DeprecatedSource(unittest.TestCase):
    def test_complete_differentials(self):
        manifest = json.loads((FIX/'manifest.json').read_text())
        self.assertEqual({e['variant'] for e in manifest['fixtures']}, set(CASES))
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for n, e in enumerate(manifest['fixtures']):
                with self.subTest(fixture=e['path']):
                    abc = FIX/e['path']; original = FIX/e['source']
                    self.assertEqual(sha(abc), e['sha256']); self.assertEqual(sha(original), e['source_sha256'])
                    self.assertEqual(original.read_text(), source(e['variant']))
                    self.assertEqual(build(module(e['variant'], Profile[e['profile']]))[0], abc.read_bytes())
                    out = root/str(n)
                    subprocess.run([BUILD/'abc_structure', abc, out, '--value-source'], check=True, capture_output=True)
                    records = [r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status') != 2]
                    self.assertEqual(len(records), 1); self.assertEqual(records[0]['source_status'], 0, records[0].get('source_failure'))
                    emitted = out/records[0]['source']
                    subprocess.run(['node', '--check', emitted], check=True, capture_output=True)
                    cases = json.loads(subprocess.check_output(['node', HARNESS, original, emitted, e['family']], text=True))['cases']
                    record_node(abc, out, original, __file__, cases, BUILD/'abc_structure', dependencies=[HARNESS])

if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--generate', action='store_true'); args = parser.parse_args()
    if args.generate: generate()
    else: unittest.main(argv=[sys.argv[0]])
