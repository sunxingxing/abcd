"""Test-only occurrence evidence. Called only after a suite's differential assertions.

ABC is the sole engine input. Originals/runners are evaluation artifacts, retained
separately. Successful recovery is necessary but never supplies an oracle itself.
"""
import gzip
import hashlib
import importlib.util
import json
import os
import shutil
from pathlib import Path
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / 'build/regression'
MAX_METHOD_EVENTS = 65536
MAX_EVENT_BYTES = 64 * 1024 * 1024
MAX_ROW_BYTES = 1024 * 1024


class OracleResourceError(ValueError):
    pass


class EventBudget:
    # Prepaid retained decoded rows, including Python object overhead. A
    # resource refusal never supplies round-trip evidence.
    def __init__(self, maximum=MAX_EVENT_BYTES):
        self.remaining = maximum

    def admit(self, row):
        charge = row['_oracle_charge_bytes']
        if charge > self.remaining:
            raise OracleResourceError('round-trip event byte resource limit')
        self.remaining -= charge



def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def artifact(directory, label, path):
    path = Path(path).resolve()
    target = directory / label
    with path.open("rb") as source, target.open("wb") as output:
        shutil.copyfileobj(source, output, 1024 * 1024)
    return {'path': str(target.resolve()), 'sha256': sha(target)}


def rows(abc, probe):
    with tempfile.TemporaryFile(mode='w+t') as errors:
        process = subprocess.Popen([str(probe), str(abc)], stdout=subprocess.PIPE,
                                   stderr=errors, text=True)
        try:
            while line := process.stdout.readline(MAX_ROW_BYTES + 1):
                if len(line) > MAX_ROW_BYTES:
                    raise OracleResourceError('probe row resource limit')
                row = json.loads(line)
                row['_oracle_charge_bytes'] = 4096 + 8 * len(line)
                if row['type'] == 'instruction':
                    yield row
            if process.wait():
                errors.seek(0)
                raise ValueError('oracle probe failed: ' + errors.read())
        finally:
            process.stdout.close()
            if process.poll() is None:
                process.kill()
                process.wait()


def method_keys(export):
    manifest = json.loads((export / 'manifest.json').read_text())
    result = {}
    for record in manifest['records']:
        if not record.get('metadata'):
            continue
        for method in json.loads((export / record['metadata']).read_text())['methods']:
            result[method['entity']] = (method['encoded_name'], method['initial_kind'],
                                        method.get('abi', {}).get('parameter_slots'))
    return result


def event_signature(row):
    # Refine the public variant family with typed nested expression/operator
    # identities. A register copy is not a property Get merely because both
    # are ScalarAssignment. Concrete entity/register numbers are not event kinds.
    def shape(value):
        if isinstance(value, dict):
            return {key: (item if key in ('type', 'op', 'kind', 'completion', 'lookup',
                'mode', 'loading', 'name_setting', 'domain', 'operation') else shape(item))
                for key, item in value.items() if isinstance(item, (dict, list)) or
                key in ('type', 'op', 'kind', 'completion', 'lookup', 'mode', 'loading', 'name_setting', 'domain', 'operation')}
        if isinstance(value, list):
            return [shape(item) for item in value]
        return None
    return {'family': row['lift']['semantic_family'], 'roles': shape(row['lift']['roles'])}


def round_trip(directory, abc, export, emitted, engine, probe, source_mode='module'):
    """Conservative event correspondence: unique method identity and complete
    ordered raw semantic-family sequence. Never LCS/width/name guessing. A
    differing sequence records a mismatch, never partial round-trip passes.
    """
    spec = importlib.util.spec_from_file_location('grouping', ROOT / 'tests/tools/grouping_bytecode.py')
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    before, after = directory / 'old.js', directory / 'input.js'
    pairs = directory / 'pairs.json'
    pairs.write_text(json.dumps([{'source_mode':source_mode, 'id': 'oracle', 'old': str(emitted), 'new': str(emitted),
                                 'adapted_old': str(before), 'adapted_new': str(after)}]))
    node_command = ['node', '--expose-internals', str(ROOT / 'tests/tools/grouping_pairs.mjs'),
                    str(pairs), str(directory / 'tokens.json')]
    if sdk.run(node_command, directory / 'tokens.log'):
        return {}, [{'kind': 'source-adaptation'}]
    output = directory / 'recompiled.abc'
    command = [sdk.ES2ABC] + ([] if source_mode == 'script' else ['--module']) + ['--merge-abc', '--opt-level=0',
               '--extension=js', '--record-name=isaOracle', '--source-file=input.js',
               '--output=' + str(output), str(after)]
    if sdk.run(command, directory / 'compile.log'):
        return {}, [{'kind': 'compiler', 'command': command}]
    disasm = directory / 'recompiled.pa'
    if sdk.run([sdk.DISASM, str(output), str(disasm)], directory / 'disasm.log'):
        return {}, [{'kind': 'disassembler'}]
    target = directory / 'structure'
    if sdk.run([str(engine), str(output), str(target)], directory / 'structure.log'):
        return {}, [{'kind': 'recompiled-metadata'}]
    originals, recompiled = method_keys(export), method_keys(target)
    keys = {}
    for method, key in recompiled.items():
        keys.setdefault(key, []).append(method)
    old_keys = {}
    for method, key in originals.items():
        old_keys.setdefault(key, []).append(method)
    sequences = {}
    budget = EventBudget()
    for row in rows(output, probe):
        sequence = sequences.setdefault(row['method'], [])
        if len(sequence) == MAX_METHOD_EVENTS:
            raise ValueError('round-trip method event resource limit')
        budget.admit(row)
        sequence.append(row)
    matched, mismatches = {}, []
    current, sequence = None, []

    def compare(method, sequence):
        key = originals.get(method)
        targets = keys.get(key, [])
        if len(targets) != 1 or len(old_keys.get(key, [])) != 1:
            mismatches.append({'method': method, 'kind': 'ambiguous-or-missing-method', 'key': key})
            return
        other = sequences.get(targets[0], [])
        families = lambda rs: [event_signature(r) if r['lift']['status'] == 'ok' else None for r in rs]
        def topology(rs):
            positions = {r['pc']: i for i, r in enumerate(rs)}
            return [(positions.get(r.get('branch_target'), r.get('branch_target')),
                positions.get(r['lift'].get('exception_exit', {}).get('handler_pc'))
                    if isinstance(r['lift'].get('exception_exit'), dict) else None,
                r['lift'].get('exceptional_completion')) for r in rs]
        if not sequence or any(f is None for f in families(sequence) + families(other)) or families(sequence) != families(other) or topology(sequence) != topology(other):
            mismatches.append({'method': method, 'target': targets[0], 'kind': 'event-sequence',
                               'original': families(sequence), 'recompiled': families(other),
                               'original_topology': topology(sequence), 'recompiled_topology': topology(other)})
            return
        for first, second in zip(sequence, other):
            matched[(method, first['pc'])] = {'status': 'pass', 'method': second['method'],
                'pc': second['pc'], 'profile': second['profile'],
                'opcode': second['encoding']['opcode'],
                'semantic_family': second['lift']['semantic_family']}

    for row in rows(abc, probe):
        if current is not None and current != row['method']:
            compare(current, sequence)
            sequence = []
        current = row['method']
        if len(sequence) == MAX_METHOD_EVENTS:
            raise ValueError('round-trip method event resource limit')
        budget.admit(row)
        sequence.append(row)
    if current is not None:
        compare(current, sequence)
    (directory / 'round-trip.json').write_text(json.dumps({
        'compiler': {'path': sdk.ES2ABC, 'sha256': sha(sdk.ES2ABC)},
        'disassembler': {'path': sdk.DISASM, 'sha256': sha(sdk.DISASM)},
        'command': command, 'abc': artifact(directory, 'round-trip.abc', output),
        'mismatches': mismatches, 'events': [{'method': method, 'pc': pc, 'witness': witness}
            for (method, pc), witness in sorted(matched.items())]}, indent=2) + '\n')
    return matched, mismatches


def record_node(abc, export, original, producer, cases, engine, exclusions=(), records=None, dependencies=()):
    destination = os.environ.get('ABCD_ISA_ORACLE_DIR')
    if not destination:
        return
    if not isinstance(cases, int) or cases <= 0:
        raise ValueError('oracle needs executed positive case count')
    abc, export, original, producer, engine = map(lambda p: Path(p).resolve(),
                                                 (abc, export, original, producer, engine))
    probe = BUILD / 'isa_coverage_probe'
    manifest = json.loads((export / 'manifest.json').read_text())
    complete_ids = {r['entity'] for r in manifest['records'] if r.get('source_status') == 0}
    covered = set(records) if records is not None else complete_ids
    if not covered or not covered.issubset(complete_ids):
        raise ValueError('oracle covered record missing or incomplete')
    if records is None and len(complete_ids) != 1:
        raise ValueError('multi-record differential requires explicit covered records')
    digest = sha(abc)
    if manifest['sha256'] != digest:
        raise ValueError('oracle ABC fingerprint mismatch')
    Path(destination).mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix=producer.stem + '-', dir=destination)).resolve()
    provenance = {'schema': 1, 'writer': {'path': str(Path(__file__).resolve()), 'sha256': sha(__file__)}, 'producer': {'path': str(producer), 'sha256': sha(producer)},
                  'engine': {'path': str(engine), 'sha256': sha(engine)},
                  'probe': {'path': str(probe), 'sha256': sha(probe)},
                  'abc': artifact(directory, 'input.abc', abc),
                  'manifest': artifact(directory, 'manifest.json', export / 'manifest.json'),
                  'original': artifact(directory, 'original.js', original),
                  'node_version': subprocess.check_output(['node', '--version'], text=True).strip(),
                  'contract': manifest['source_contract'], 'cases': cases,
                  'exclusions': list(exclusions), 'records': [],
                  'dependencies': [{'path':str(Path(p).resolve()),'sha256':sha(p)} for p in dependencies]}
    complete = {}
    trip = {}
    for record in manifest['records']:
        if record['entity'] not in covered:
            continue
        emitted = export / record['source']
        rid = record['entity']
        source_mode='script' if record.get('source_language') == 'javascript-script' else 'module'
        copied = artifact(directory, f'{rid}.js' if source_mode=='script' else f'{rid}.mjs', emitted)
        complete[rid] = copied
        subdir = directory / str(rid)
        subdir.mkdir()
        try:
            correspondence, mismatch = round_trip(subdir, abc, export, emitted, engine, probe,
                source_mode)
        except OracleResourceError as error:
            correspondence, mismatch = {}, [{'kind': 'resource', 'reason': str(error)}]
        trip[rid] = correspondence
        provenance['records'].append({'record': rid, 'emitted': copied, 'source_mode':source_mode,
                                      'round_trip_mismatches': mismatch,
                                      'round_trip': artifact(directory, f'{rid}-round-trip.json', subdir/'round-trip.json')
                                          if (subdir/'round-trip.json').exists() else None})
    if not complete:
        raise ValueError('oracle cannot certify incomplete records')
    with gzip.open(directory / 'occurrences.jsonl.gz', 'wt') as output:
        for row in rows(abc, probe):
            if row['sha256'] != digest:
                raise ValueError('oracle probe fingerprint mismatch')
            if row['record'] not in complete or row['lift']['status'] != 'ok':
                continue
            identity = {k: row[k] for k in ('sha256', 'profile', 'record', 'method', 'pc')}
            identity.update(semantic_family=row['lift']['semantic_family'],
                            opcode=row['encoding']['opcode'],
                            node={'status': 'pass'},
                            ark_round_trip=trip[row['record']].get((row['method'], row['pc'])),
                            ark_runtime=None)
            output.write(json.dumps(identity, separators=(',', ':')) + '\n')
    provenance['occurrences'] = {'path': str(directory / 'occurrences.jsonl.gz'),
                                 'sha256': sha(directory / 'occurrences.jsonl.gz')}
    # Publish only after probing, compilation and the complete stream finished.
    (directory / 'evidence.json').write_text(json.dumps(provenance, indent=2) + '\n')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    for name in ('abc', 'export', 'original', 'producer', 'engine'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--cases', type=int, required=True)
    parser.add_argument('--exclusion', action='append', default=[])
    parser.add_argument('--record', action='append', type=int)
    args = parser.parse_args()
    record_node(args.abc, args.export, args.original, args.producer,
                args.cases, args.engine, args.exclusion, args.record)
