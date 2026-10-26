#!/usr/bin/env python3
"""Deprecated encodings: measured decode/lift, never inferred execution support."""
import argparse
from functools import lru_cache
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
import builder
BUILD = ROOT/'build/regression'

def run(args):
    return subprocess.run([str(a) for a in args], capture_output=True, text=True,
                          timeout=90, cwd=ROOT)

_CATALOG = None
def catalog():
    global _CATALOG
    if _CATALOG is None:
        names = {int(v): n for n, v in re.findall(r'\bk(\w+)\s*=\s*(\d+)',
            (BUILD/'generated/abcd/abc/generated_isa.h').read_text())}
        result = run([BUILD/'isa_coverage_probe', '--catalog'])
        assert result.returncode == 0, result.stderr
        _CATALOG = {names[r['opcode']]: r for line in result.stdout.splitlines()
                    if (r := json.loads(line))}
    return _CATALOG

FIX = ROOT/'tests/fixtures/isa/deprecated'
SPEC = ROOT/'tests/isa/deprecated_variants.json'
SDK = Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony')
DISASM = SDK/'toolchains/ark_disasm'
RUNTIME = ROOT/'references/openharmony/arkcompiler/ets_runtime/ecmascript'
PROFILES = {p: i + 1 for i, p in enumerate(builder.Profile)}

@lru_cache(maxsize=32)
def source(path):
    return (ROOT/path).read_text()

@lru_cache(maxsize=512)
def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def citation(path, pattern, required=True):
    path = str(Path(path).relative_to(ROOT)) if Path(path).is_absolute() else path
    text = source(path)
    match = re.search(pattern, text)
    if not match:
        assert not required, (path, pattern)
        return None
    return {'path': path, 'line': text.count('\n', 0, match.start()) + 1,
            'sha256': sha(ROOT/path)}

def selected():
    return {n: s for n, s in catalog().items() if n.startswith('DEPRECATED_')}

def specimen(spec, profile, count=1):
    # All encoded registers refer to initialized v0. ID references use the real
    # regional pool: method 0, string 1, tagged literal 2. This is container/lift
    # evidence, not a forged class/generator/module or callable runtime protocol.
    bits = shift = 0
    for index, operand in enumerate(spec['format']):
        value = 1 if operand['kind'] == 5 else 2 if operand['kind'] == 6 else 0
        if spec['opcode'] == catalog()['DEPRECATED_CALLTHISRANGE_PREF_IMM16_V8']['opcode'] and index == 0:
            value = 1  # Pinned handler subtracts the receiver from this count.
        bits |= value << shift
        shift += operand['width']
    operands = bits.to_bytes((shift + 7)//8, 'little')
    target = builder.Instruction(spec['opcode'], operands, opcode_width=2)
    c = catalog()
    prefix = (builder.Instruction(c['LDUNDEFINED']['opcode']),
              builder.Instruction(c['STA_V8']['opcode'], b'\0'))
    environment = {c[n]['opcode'] for n in (
        'DEPRECATED_LDLEXENV_PREF_NONE', 'DEPRECATED_POPLEXENV_PREF_NONE',
        'DEPRECATED_STLEXVAR_PREF_IMM4_IMM4_V8',
        'DEPRECATED_STLEXVAR_PREF_IMM8_IMM8_V8',
        'DEPRECATED_STLEXVAR_PREF_IMM16_IMM16_V8')}
    if spec['opcode'] in environment:
        prefix += (builder.Instruction(c['NEWLEXENV_IMM8']['opcode'], b'\1'),)
    return builder.Module(prefix + (target,)*count +
        (builder.Instruction(c['RETURNUNDEFINED']['opcode']),),
        registers=8, pool_strings=('p',), empty_literals=1,
        name='isa_deprecated', profile=profile)

def measure(path, temp):
    manifests = []
    for flag, options in [('language_source_v10', [])]:
        out = temp/flag
        result = run([BUILD/'abc_structure', path, out, '--value-source', *options])
        assert result.returncode == 0, (path, result.stderr)
        manifest = out/'manifest.json'
        assert json.loads(manifest.read_text())['source_contract'] == 'language_source_v10'
        manifests.append(manifest)
    result = run([BUILD/'isa_coverage_probe', path, *manifests])
    assert result.returncode == 0, (path, result.stderr)
    return [r for line in result.stdout.splitlines()
            if (r := json.loads(line))['type'] == 'instruction']

def evidence(name):
    isa = 'references/openharmony/arkcompiler/runtime_core/isa/isa.yaml'
    rt = RUNTIME/'interpreter/interpreter-inl.cpp'
    sig = name.split('_PREF_')[0].lower().replace('_', '.')
    cpp = citation(rt, r'HANDLE_OPCODE\(' + name + r'\)')
    text = source(cpp['path']); begin = text.find('HANDLE_OPCODE('+name+')')
    end = text.find('HANDLE_OPCODE(', begin + 1)
    body = text[begin:end]
    assert 'FATAL' not in body and ('GET_' in body or 'READ_INST_' in body or 'SET_ACC' in body or 'SlowRuntimeStub::' in body), name
    camel = 'Handle' + ''.join(x.capitalize() for x in name.split('_'))
    # AOT metadata and lowering are separate from C++/assembly/baseline paths.
    # The current metadata loop stops at secondary 45 (dynamicimport), while
    # asyncgeneratorreject is secondary 46. No specialized deprecated lowering
    # exists in SlowPathLowering; default is a no-op, not execution evidence.
    aot = RUNTIME/'compiler/slowpath_lowering.cpp'
    lower = re.search(r'case EcmaOpcode::'+name+r':', source(str(aot.relative_to(ROOT))))
    assert lower is None, name
    return {
        'isa': citation(isa, r'- sig: '+re.escape(sig)+r'(?=\s|$)'),
        'interpreter': dict(cpp, status='implemented',
            runtime_calls=sorted(set(re.findall(r'(?:SlowRuntimeStub|FastRuntimeStub|RuntimeStub)::(\w+)', body)))),
        'dispatch': citation(RUNTIME/'interpreter/templates/deprecated_instruction_dispatch.inl',
                             r'&&HANDLE_'+name+r','),
        'assembly_stub': citation(RUNTIME/'compiler/interpreter_stub.cpp',
                                 r'DECLARE_ASM_HANDLER\('+camel+r'\)'),
        'assembly': citation(RUNTIME/'interpreter/interpreter_assembly.cpp',
                            r'void InterpreterAssembly::'+camel+r'\('),
        'baseline': citation(RUNTIME/'compiler/baseline/baseline_compiler.cpp',
                            r'BYTECODE_BASELINE_HANDLER_IMPLEMENT\('+name+r'\)', False),
        'aot': {'specialized_lowering': None,
            'metadata_initialized': name != 'DEPRECATED_ASYNCGENERATORREJECT_PREF_V8_V8',
            'last_opcode': citation(RUNTIME/'compiler/bytecodes.h', r'LAST_DEPRECATED_OPCODE\s*='),
            'metadata_loop': citation(RUNTIME/'compiler/bytecodes.cpp', r'last = .*LAST_DEPRECATED_OPCODE'),
            'general_gate': citation(RUNTIME/'compiler/bytecode_circuit_builder.cpp', r'else if \(bytecodeInfo.IsGeneral\(\)\)'),
            'lowering': citation(aot, r'void SlowPathLowering::Lower\(GateRef gate\)')},
        'all_path_non_executable': False,
        'encoding_profiles': [{'profile': PROFILES[p], 'version': list(p.value)} for p in builder.Profile],
        'profile_semantics': '9/11.0.2 decode only (kUnsupportedProfile); 12.0.2/12.0.6/13.0.1/24 use pinned semantic adapters; no historical frontend-emission claim',
        'execution': {'status': 'not_run', 'reason': 'No ark_js_vm in the pinned DevEco SDK or PATH. ark_aot_compiler is a compiler, not a JS execution oracle.',
            'fixture_scope': 'Initialized scalar v0 and valid container/pool references only. Environment/call/iterator/generator/class/home-object/module operations additionally need their actual dynamic inputs/protocols; no execution validity or source equivalence inferred.'}}

def generate():
    FIX.mkdir(parents=True, exist_ok=True)
    variants, fixtures = [], []
    for name, spec in selected().items():
        row = {'name': name, **{k: spec[k] for k in ('opcode', 'prefix', 'size', 'format')},
               'evidence': evidence(name), 'measurements': []}
        for profile in builder.Profile:
            path = FIX/(name.lower()+'-'+profile.name.lower()+'.abc')
            blob, _ = builder.build(specimen(spec, profile)); path.write_bytes(blob)
            with tempfile.TemporaryDirectory() as d:
                temp = Path(d)
                dis = run([DISASM, path, temp/'out.pa'])
                assert dis.returncode == 0, (name, profile, dis.stderr)
                mnemonic = name.split('_PREF_')[0].lower().replace('_', '.')
                text = (temp/'out.pa').read_text()
                assert len(re.findall(r'^\s*'+re.escape(mnemonic)+r'(?=\s|$)', text, re.M)) == 1, (name, text)
                instructions = measure(path, temp)
            hit = [r for r in instructions if r['encoding']['opcode'] == spec['opcode']]
            assert len(hit) == 1 and hit[0]['profile'] == PROFILES[profile], (name, profile, hit)
            r = hit[0]
            assert r['decode'] == 'ok' and r['encoding']['prefix'] == spec['prefix']
            measurement = {'path': path.name, 'sha256': hashlib.sha256(blob).hexdigest(),
                'profile': r['profile'], 'version': list(profile.value), 'pc': r['pc'],
                'encoding': r['encoding'], 'operands': r['operands'], 'lift': r['lift'], 'recovery': r['recovery'],
                'disassembler': {'status': 'decoded', 'mnemonic': mnemonic}, 'oracles': {}}
            row['measurements'].append(measurement)
            fixtures.append({'path': str(path.relative_to(ROOT)), 'sha256': measurement['sha256'],
                'profile': r['profile'], 'opcodes': [spec['opcode']], 'classification': 'Unmeasured',
                'oracles': {}, 'note': 'Constructed encoding/raw lift only; execution and source semantics not verified. Deprecated does not imply NonExecutable.'})
        variants.append(row)
        print(name, 'profiles=6', flush=True)
    SPEC.write_text(json.dumps({'schema': 1, 'baseline': '41f6b2a1',
        'scope': '47 deprecated variants, six separately measured container profiles',
        'recovery_contract': 'language_source_v10',
        'disassembler': str(DISASM), 'disassembler_sha256': sha(DISASM),
        'variants': variants}, indent=2)+'\n')
    registry = ROOT/'tests/isa/coverage_manifest.json'
    entries = json.loads(registry.read_text())
    prefix = str(FIX.relative_to(ROOT))+'/'
    entries['fixtures'] = [r for r in entries['fixtures'] if not r['path'].startswith(prefix)] + fixtures
    registry.write_text(json.dumps(entries, indent=2)+'\n')


class Deprecated(unittest.TestCase):
    def test_catalog_and_all_profile_fixtures(self):
        data = json.loads(SPEC.read_text())
        self.assertEqual(data['recovery_contract'], 'language_source_v10')
        specs = data['variants']
        self.assertEqual(len(specs), 47)
        self.assertEqual({s['name'] for s in specs}, set(selected()))
        self.assertEqual(sha(DISASM), data['disassembler_sha256'])
        registry = {e['path']: e for e in json.loads((ROOT/'tests/isa/coverage_manifest.json').read_text())['fixtures']}
        with tempfile.TemporaryDirectory() as d:
            temp = Path(d)
            for s in specs:
                self.assertEqual(s['evidence'], evidence(s['name']))
                self.assertFalse(s['evidence']['all_path_non_executable'])
                self.assertEqual(len(s['measurements']), 6)
                for e, profile in zip(s['measurements'], builder.Profile):
                    path = FIX/e['path']
                    self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), e['sha256'])
                    self.assertEqual(path.read_bytes(), builder.build(specimen(s, profile))[0])
                    self.assertEqual(e['version'], list(profile.value))
                    self.assertEqual(e['profile'], PROFILES[profile])
                    entry = registry[str(path.relative_to(ROOT))]
                    self.assertEqual(entry['sha256'], e['sha256'])
                    self.assertEqual(entry['opcodes'], [s['opcode']])
                    self.assertEqual(entry['profile'], e['profile'])
                    self.assertEqual(entry['oracles'], {})
                    self.assertNotEqual(entry['classification'], 'NonExecutable')
                    dis = run([DISASM, path, temp/'out.pa'])
                    self.assertEqual(dis.returncode, 0, dis.stderr)
                    self.assertEqual(len(re.findall(r'^\s*'+re.escape(e['disassembler']['mnemonic'])+r'(?=\s|$)',
                        (temp/'out.pa').read_text(), re.M)), 1)
                    with tempfile.TemporaryDirectory(dir=temp) as each:
                        rows = measure(path, Path(each))
                    hit = [r for r in rows if r['encoding']['opcode'] == s['opcode']]
                    self.assertEqual(len(hit), 1)
                    row = hit[0]
                    self.assertEqual(row['decode'], 'ok')
                    self.assertEqual(row['profile'], e['profile'])
                    for key in ('encoding', 'operands', 'pc', 'lift', 'recovery'):
                        self.assertEqual(row[key], e[key], (s['name'], profile, key))
                    self.assertNotIn('non_executable', row['lift'])
                    if profile in (builder.Profile.DYNAMIC9, builder.Profile.DYNAMIC1102):
                        self.assertEqual(row['lift']['cause']['value'], 0)  # kUnsupportedProfile
                    elif row['lift']['status'] == 'refused':
                        self.assertEqual(row['lift']['cause']['value'], 1)  # kUnsupportedInstruction

    def test_abc_decode_all(self):
        # Exercise the actual corpus decoder in addition to the raw probe's
        # per-instruction decoder; a disassembler exit alone is insufficient.
        data = json.loads(SPEC.read_text())
        with tempfile.TemporaryDirectory() as d:
            index = Path(d)/'index.json'
            scanned = run([BUILD/'abc_scan', index, 'deprecated', FIX])
            self.assertEqual(scanned.returncode, 0, scanned.stderr)
            decoded = run([BUILD/'abc_decode', index, 'deprecated', FIX])
            self.assertEqual(decoded.returncode, 0, decoded.stderr)
            report = json.loads(decoded.stdout)
            self.assertEqual(report['failed_methods'], 0)
            self.assertEqual(report['failed'], 0)
            self.assertEqual(report['decoded_methods'], 47*6)
            self.assertEqual(report['instructions'], sum(len(specimen(s, p).instructions)
                for s in data['variants'] for p in builder.Profile))
            self.assertTrue(all(r['outcome'] == 'decoded' for r in report['results']))

    def test_mutations_and_budget(self):
        import coverage_matrix as matrix
        spec = catalog()['DEPRECATED_NEG_PREF_V8']
        module = specimen(spec, builder.Profile.DYNAMIC9)
        blob, _ = builder.build(module)
        registry = {'sha256': hashlib.sha256(blob).hexdigest(), 'opcodes': [spec['opcode']],
                    'classification': 'Unmeasured', 'oracles': {}}
        matrix.validate_registry(registry, registry['sha256'], {spec['opcode']})
        for digest, seen in [('bad', {spec['opcode']}), (registry['sha256'], set())]:
            with self.assertRaises(ValueError): matrix.validate_registry(registry, digest, seen)
        bad = dict(registry, classification='SupportedVerified')
        with self.assertRaises(ValueError): matrix.validate_registry(bad, registry['sha256'], {spec['opcode']})
        entry = next(s for s in json.loads(SPEC.read_text())['variants'] if s['name'] == 'DEPRECATED_NEG_PREF_V8')
        encoded = entry['measurements'][0]['encoding']
        for key in ('prefix', 'size'):
            wrong = dict(encoded); wrong[key] += 1
            with self.assertRaises(ValueError): matrix.validate_encoding({'encoding': wrong}, encoded)
        wrong = dict(encoded, format=[dict(o) for o in encoded['format']])
        wrong['format'][0]['width'] += 8
        with self.assertRaises(ValueError): matrix.validate_encoding({'encoding': wrong}, encoded)
        for count in (200, 201, 400, 800):
            module = specimen(spec, builder.Profile.DYNAMIC1206, count)
            expected = builder.build(module)[0]
            low, high = 0, 1024*1024
            while low < high:
                mid = (low + high)//2
                try: builder.build(module, mid); high = mid
                except builder.BuildError: low = mid + 1
            self.assertEqual(builder.build(module, low)[0], expected)
            with self.assertRaises(builder.BuildError): builder.build(module, low - 1)
            with tempfile.TemporaryDirectory() as d:
                path = Path(d)/'scale.abc'; path.write_bytes(expected)
                result = run([BUILD/'isa_coverage_probe', path])
                self.assertEqual(result.returncode, 0, result.stderr)
                rows = [r for line in result.stdout.splitlines() if (r := json.loads(line))['type'] == 'instruction']
                hit = [r for r in rows if r['encoding']['opcode'] == spec['opcode']]
                self.assertEqual(len(hit), count)
                self.assertTrue(all(r['lift']['status'] == 'ok' for r in hit))

    def test_profile_layout(self):
        # The writer must encode the selected container layout, not patch the
        # version of a 12.0.6 file after its checksum/layout have been produced.
        for profile in builder.Profile:
            module = builder.Module((builder.Instruction(catalog()["RETURNUNDEFINED"]["opcode"]),), profile=profile)
            blob, _ = builder.build(module)
            self.assertEqual(blob[12:16], bytes(profile.value))
            literal_count, literal_offset = __import__('struct').unpack_from('<II', blob, 44)
            if profile.literal_arrays_in_header:
                self.assertEqual(literal_count, 1)
                self.assertGreater(literal_offset, 60)
            else:
                self.assertEqual((literal_count, literal_offset), (builder.U32_MAX,)*2)
        with self.assertRaises(builder.BuildError):
            builder.build(builder.Module((), profile=(11, 0, 0, 0)))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--generate', action='store_true')
    args = parser.parse_args()
    if args.generate: generate()
    else: unittest.main(argv=[sys.argv[0]])
