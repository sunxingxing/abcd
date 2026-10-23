#!/usr/bin/env python3
"""Measured ISA ledger. Names label generated encodings; never infer lifting from names.
One subprocess at a time; instruction JSONL is streamed/gzipped. Recovery is the
real abc_structure pipeline, joined by fingerprint + record + method + original PC.
"""
import argparse
import collections
import gzip
import hashlib
import json
import pathlib
import re
import subprocess
import sys
sys.dont_write_bytecode = True
import os
import tempfile
import xml.etree.ElementTree as ET
from oracle_import import OracleIndex

CLASSES = ('Unmeasured', 'SupportedUnverified', 'SupportedVerified',
           'UnsupportedPremiseFree', 'NeedsDecision', 'NonExecutable')
FAMILIES = ('Constant', 'Iterator', 'Creation', 'Binary', 'Unary', 'Comparison',
            'RuntimeCall', 'Throw', 'Call', 'Definition', 'ObjectAccess', 'String',
            'Jump', 'Move', 'RegisterLoad', 'RegisterStore', 'Immediate', 'Return', 'Nop')

def fingerprint(path):
    h = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def catalog(probe, generated):
    names = {int(number): name for name, number in re.findall(
        r'\bk(\w+)\s*=\s*(\d+)', pathlib.Path(generated).read_text())}
    rows = [json.loads(line) for line in subprocess.check_output(
        [str(probe), '--catalog'], text=True).splitlines()]
    if len(rows) != 332 or len({r['opcode'] for r in rows}) != 332:
        raise ValueError('pinned catalog must contain 332 distinct variants')
    for row in rows:
        row['name'] = names[row['opcode']]
    return rows

def validate_encoding(row, spec):
    for key in ('opcode', 'prefix', 'opcode_byte', 'size', 'format', 'acc_read', 'acc_write'):
        if row['encoding'][key] != spec[key]:
            raise ValueError(f'encoding mismatch: {key}')

def validate_registry(entry, digest, seen):
    if entry['sha256'] != digest:
        raise ValueError('stale fixture hash')
    if not set(entry.get('opcodes', ())).issubset(seen):
        raise ValueError('registered opcode missing from decode')
    for oracle in entry.get('oracles', {}).values():
        if oracle.get('status') == 'pass' and (oracle.get('skipped') or not oracle.get('evidence')):
            raise ValueError('oracle pass without execution evidence')
    if entry.get('classification') == 'SupportedVerified':
        if entry.get('lift_status') != 'ok' or entry.get('recovery_status') != 'complete':
            raise ValueError('refusal cannot count as support')
        raise ValueError('fixture labels cannot replace imported occurrence evidence')

def validate_non_executable(entry, row, root):
    """A constructed refusal plus the pinned citation, never refusal alone."""
    if entry.get('classification') != 'NonExecutable':
        return False
    evidence = root / entry['evidence']
    if fingerprint(evidence) != entry.get('evidence_sha256'):
        raise ValueError('stale runtime evidence')
    if row['profile'] != entry['profile'] or row['encoding']['opcode'] not in entry['opcodes']:
        return False
    lift = row['lift']
    if (lift['status'] != 'refused' or lift['cause'].get('domain') != 'SemanticError'
        or lift['cause'].get('value') != 7 or
        lift.get('non_executable') != {'opcode': row['encoding']['opcode'], 'evidence': entry['evidence_id']}):
        raise ValueError('non-executable classification without typed runtime refusal')
    for flag in ('previous','v4'):
        failure = row['recovery'][flag]['record_first_refusal']
        if not failure or failure.get('non_executable') != lift['non_executable']:
            raise ValueError('non-executable classification without record refusal')
    return True

def recovery_manifest(structure, path, output, flags, digest):
    manifest = output / 'manifest.json'
    if not manifest.exists():
        output.parent.mkdir(parents=True, exist_ok=True)
        with open(str(output) + '.log', 'w') as log:
            result = subprocess.run([str(structure), str(path), str(output), '--value-source', *flags],
                                    stdout=log, stderr=subprocess.STDOUT)
        if not manifest.exists():
            return {'sha256': digest, 'records': [], 'pipeline_exit': result.returncode}
    data = json.loads(manifest.read_text())
    if data['sha256'] != digest:
        raise ValueError('recovery fingerprint mismatch')
    expected_contract = 'language_source_v10' if '--language-source' in flags else 'language_numeric_constructors_v1'
    if data.get('source_contract') != expected_contract:
        raise ValueError('recovery contract mismatch')
    return data

def inputs(root):
    cfg = json.loads((root / 'tests/corpus/source-cohorts.json').read_text())
    cohort = next(c for c in cfg['cohorts'] if c['id'] == 'harmony-exploration-2026-10-12')
    for i, item in enumerate(cohort['inputs']):
        yield 'exploration', f'o{i}', pathlib.Path(cfg['roots'][cohort['root']]) / item['path']
    for group, base in [('paired', root / 'samples/abc_with_code'), ('fixtures', root / 'tests')]:
        for i, path in enumerate(sorted(base.rglob('*.abc'))):
            # User-owned large input is explicitly outside this measurement.
            if path == root / 'tests/samples/big.abc':
                continue
            yield group, str(i), path

def run(args):
    root = pathlib.Path(args.root).resolve()
    build = root / 'build/regression'
    out = pathlib.Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=True)
    specs = catalog(build / 'isa_coverage_probe', build / 'generated/abcd/abc/generated_isa.h')
    by_opcode = {s['opcode']: s for s in specs}
    registry_document = json.loads((root / 'tests/isa/coverage_manifest.json').read_text())
    registry = registry_document['fixtures']
    registered = {str((root / e['path']).resolve()): e for e in registry}
    stats = {}; files = []; first = []; failures = []; unattributed = []; non_executable = {}
    if any(e.get('classification') == 'NonExecutable' for e in registry):
        subprocess.run([sys.executable, root / 'tests/isa/constructed_branches_test.py'], check=True)
    flags = {'previous': ['--language-numeric', '--language-constructors'], 'v4': ['--language-source']}
    def state(profile, opcode):
        key = (profile, opcode)
        if key not in stats:
            stats[key] = {'occurrences': 0, 'lift': collections.Counter(), 'semantic_families': collections.Counter(),
                          'recovery': collections.Counter(), 'records': set(), 'cohort_records': set(), 'group_occurrences': collections.Counter(),
                          'first': {f: set() for f in flags}, 'complete_occurrence': False,
                          'oracles': collections.Counter(), 'oracle_examples': {}}
        return stats[key]
    probe_hash = fingerprint(build / 'isa_coverage_probe')
    structure_hash = fingerprint(build / 'abc_structure')
    provenance = {'structure_sha256':structure_hash, 'flags':flags}
    cache_identity = out / 'recovery_identity.json'
    if cache_identity.exists() and json.loads(cache_identity.read_text()) != provenance:
        raise ValueError('recovery cache belongs to another binary or contracts; use a new output directory')
    if not cache_identity.exists() and (out / 'recovery').exists():
        raise ValueError('unidentified recovery cache; use a new output directory')
    cache_identity.write_text(json.dumps(provenance,sort_keys=True)+'\n')
    # Producer success and decoded occurrence identity are distinct gates.
    # A skipped test never supplies an oracle. Each run has its own transaction
    # directory; stale output from a failed/older run is not imported.
    oracle_root = pathlib.Path(tempfile.mkdtemp(prefix='oracle-run-', dir=out))
    producers = registry_document.get('oracle_producers', [])
    oracle_runs = []
    for producer in producers:
        junit = oracle_root / (producer['test'] + '.xml')
        command = ['ctest', '--test-dir', str(build), '--no-tests=error',
                   '-R', '^' + re.escape(producer['test']) + '$',
                   '--output-junit', str(junit), '--output-on-failure']
        env = dict(os.environ, ABCD_ISA_ORACLE_DIR=str(oracle_root))
        with open(oracle_root / (producer['test'] + '.log'), 'w') as log:
            subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        cases = ET.parse(junit).getroot().findall('.//testcase')
        if len(cases) != 1 or any(c.find('skipped') is not None or c.find('failure') is not None for c in cases):
            raise ValueError('oracle producer skipped or did not execute')
        evidence = [p for p in oracle_root.glob('*/evidence.json') if
                    json.loads(p.read_text())['producer']['path'] == str((root / producer['producer']).resolve())]
        if not evidence:
            raise ValueError('oracle producer supplied no occurrence evidence')
        oracle_runs.append({'test': producer['test'], 'command': command,
                            'evidence': [str(p) for p in evidence]})
    oracle_index = OracleIndex(out / 'oracle-index.sqlite', structure_hash, probe_hash,
                               [root / p['producer'] for p in producers])
    for run in oracle_runs:
        for evidence in run['evidence']:
            oracle_index.import_file(evidence)
    items = list(inputs(root))
    # Producer-generated ABCs are retained test artifacts and must be decoded
    # too; equal bytes in existing fixtures need no second measurement.
    digests = {fingerprint(path) for _, _, path in items}
    for run in oracle_runs:
        for evidence in run['evidence']:
            abc = json.loads(pathlib.Path(evidence).read_text())['abc']
            if abc['sha256'] not in digests:
                digests.add(abc['sha256'])
                items.append(('oracle_fixtures', str(len(items)), pathlib.Path(abc['path'])))
    if args.limit: items = items[:args.limit]
    for number, (group, label, path) in enumerate(items):
        print(f'{number+1}/{len(items)} {group}/{label} {path}', file=sys.stderr, flush=True)
        digest = fingerprint(path)
        manifests = {f: recovery_manifest(build / 'abc_structure', path, out / 'recovery' / group / label / f, fs, digest)
                     for f, fs in flags.items()}
        record_maps = {f: {r['entity']: r for r in m['records']} for f, m in manifests.items()}
        method_failures = {}
        for flag, records in record_maps.items():
            for record in records.values():
                failure = record.get('source_failure', {})
                if 'method' in failure:
                    method_failures[(flag, failure['method'])] = failure
                for omitted in record.get('source_omitted_members', []):
                    method_failures[(flag, omitted['method'])] = omitted
        # A refusal is attributable only at an actual original instruction and
        # its named method. No default PC zero and no guessed normalized PC.
        locations = collections.defaultdict(list)
        for flag, records in record_maps.items():
            for rid, record in records.items():
                failure = record.get('source_failure', {})
                if 'method' in failure and 'pc' in failure:
                    locations[(failure['method'], failure['pc'])].append((flag, rid, failure))
        raw = out / 'raw' / f'{digest}-{probe_hash}-{structure_hash}.jsonl.gz'; raw.parent.mkdir(exist_ok=True)
        exit_path = pathlib.Path(str(raw) + '.exit')
        if not raw.exists() or not exit_path.exists():
            temp = raw.with_suffix('.partial')
            with open(str(raw)+'.stderr', 'w') as err, gzip.open(temp, 'wt') as dest:
                probe_command = [str(build / 'isa_coverage_probe'), str(path)]
                manifest_paths = [out / 'recovery' / group / label / f / 'manifest.json' for f in flags]
                if all(p.exists() for p in manifest_paths): probe_command += [str(p) for p in manifest_paths]
                process = subprocess.Popen(probe_command, stdout=subprocess.PIPE, stderr=err, text=True)
                for line in process.stdout: dest.write(line)
                code = process.wait()
            temp.rename(raw)
            exit_path.write_text(str(code))
        probe_exit = int(exit_path.read_text())
        if probe_exit:
            failures.append({'input':str(path), 'type':'probe_exit', 'exit':probe_exit})
        enriched = out / 'occurrences' / group / f'{label}.jsonl.gz'; enriched.parent.mkdir(parents=True, exist_ok=True)
        seen = set(); count = 0; matched = set()
        with gzip.open(raw, 'rt') as stream, gzip.open(enriched, 'wt') as dest:
            for line in stream:
                row = json.loads(line)
                if row['type'] != 'instruction':
                    if row['type'] != 'method_no_code': failures.append({'input': str(path), **row})
                    dest.write(line); continue
                if row['sha256'] != digest: raise ValueError('raw fingerprint mismatch')
                opcode = row['encoding']['opcode']; validate_encoding(row, by_opcode[opcode])
                seen.add(opcode); count += 1
                s = state(row['profile'], opcode); s['occurrences'] += 1; s['group_occurrences'][group] += 1
                lifted = row['lift']; outcome = lifted['status']
                if 'cause' in lifted: outcome += ':' + json.dumps({k:v for k,v in lifted['cause'].items() if k != 'pc'}, sort_keys=True)
                s['lift'][outcome] += 1
                if 'semantic_family' in lifted: s['semantic_families'][lifted['semantic_family']] += 1
                identity = (digest, row['record']); s['records'].add(identity)
                if group == 'exploration': s['cohort_records'].add(identity)
                row['recovery'] = {}
                for flag, records in record_maps.items():
                    r = records.get(row['record'], {})
                    status = r.get('source_status')
                    row['recovery'][flag] = {'record_status': status, 'record_first_refusal': r.get('source_failure'),
                                             'method_status': 'refused' if (flag, row['method']) in method_failures else 'not_individually_reported',
                                             'method_first_refusal': method_failures.get((flag, row['method']))}
                    # Record completion isn't promoted to method/oracle evidence.
                    s['recovery'][f'{flag}:{status}'] += 1
                    if status == 0 and lifted['status'] == 'ok': s['complete_occurrence'] = True
                for flag, rid, failure in locations.get((row['method'], row['pc']), ()):
                    entry = {'group': group, 'input': label, 'record': rid, 'method': row['method'], 'pc': row['pc'],
                             'opcode': opcode, 'name': by_opcode[opcode]['name'], 'flag': flag, 'failure': failure, 'attribution': 'located_first_refusal'}
                    first.append(entry); matched.add((flag, rid))
                    if group == 'exploration': s['first'][flag].add((digest, rid))
                registry_entry = registered.get(str(path.resolve()))
                if registry_entry and validate_non_executable(registry_entry, row, root):
                    non_executable[(row['profile'], opcode)] = {'evidence': registry_entry['evidence'], 'evidence_sha256': registry_entry['evidence_sha256'], 'evidence_id': registry_entry['evidence_id'], 'fixture': registry_entry['path'], 'refusal_test': registry_entry['refusal_test']}
                row['oracles'] = oracle_index.lookup(row)
                for name, evidence in row['oracles'].items():
                    if evidence is not None:
                        s['oracles'][name] += 1
                        s['oracle_examples'].setdefault(name, evidence['evidence'])
                dest.write(json.dumps(row, separators=(',', ':')) + '\n')
        if str(path.resolve()) in registered: validate_registry(registered[str(path.resolve())], digest, seen)
        for flag, records in record_maps.items():
            for rid, r in records.items():
                if r.get('source_status') in (3, 4) and (flag, rid) not in matched:
                    unattributed.append({'group':group, 'input':label, 'record':rid, 'flag':flag,
                        'failure':r.get('source_failure'), 'reason':'no_matching_original_instruction_witness'})
        files.append({'group': group, 'label': label, 'path': str(path), 'sha256': digest, 'instructions': count,
                      'variants': len(seen), 'probe_exit':probe_exit, 'recovery': {f: collections.Counter(str(r.get('source_status')) for r in m['records'] if r.get('planned_source')) for f,m in manifests.items()}})
    profiles = sorted({p for p, _ in stats}) or [specs[0]['profile']]
    rows = []
    for profile in profiles:
        for spec in specs:
            s = state(profile, spec['opcode'])
            row = dict(spec, profile=profile)
            row.update(occurrences=s['occurrences'], lift=dict(s['lift']), semantic_families=dict(s['semantic_families']),
                       recovery=dict(s['recovery']), group_occurrences=dict(s['group_occurrences']), records_containing=len(s['records']),
                       cohort_records_containing=len(s['cohort_records']),
                       current_first_refusal={f: len(v) for f,v in s['first'].items()},
                       oracles={name: {'pass_occurrences': s['oracles'][name], 'example': s['oracle_examples'][name]}
                                if s['oracles'][name] else None for name in ('node', 'ark_round_trip', 'ark_runtime')},
                       classification=('SupportedVerified' if any(s['oracles'].values()) else
                                       'SupportedUnverified') if s['complete_occurrence'] else 'Unmeasured')
            if (profile, spec['opcode']) in non_executable:
                if s['complete_occurrence'] or s['lift'].get('ok',0):
                    raise ValueError('non-executable evidence conflicts with successful lift')
                row['classification'] = 'NonExecutable'
                row['classification_evidence'] = non_executable[(profile, spec['opcode'])]
            rows.append(row)
    profile_rows = rows
    variants = []
    members_by_opcode = collections.defaultdict(list)
    for row in profile_rows:
        members_by_opcode[row['opcode']].append(row)
    for spec in specs:
        members = members_by_opcode[spec['opcode']]
        merged = dict(spec)
        merged.pop('profile', None)
        merged['profiles'] = members
        merged['occurrences'] = sum(r['occurrences'] for r in members)
        merged['verified_profiles'] = [r['profile'] for r in members if r['classification'] == 'SupportedVerified']
        merged['classification'] = 'SupportedVerified' if merged['verified_profiles'] else 'SupportedUnverified' if any(r['classification'] == 'SupportedUnverified' for r in members) else 'Unmeasured'
        if any(r['classification'] == 'NonExecutable' for r in members):
            if merged['classification'] in ('SupportedUnverified', 'SupportedVerified'):
                raise ValueError('profile conflict requires separate classification')
            merged['classification'] = 'NonExecutable'
        variants.append(merged)
    family_rollups = []
    for family, name in enumerate(FAMILIES):
        codes = {s['opcode'] for s in specs if s['family'] == family}
        sources = [s for (p, op), s in stats.items() if op in codes]
        family_rollups.append({'family': name, 'variants': len(codes),
            'observed_variants': len({op for (p,op),s in stats.items() if op in codes and s['occurrences']}),
            'occurrences': sum(s['occurrences'] for s in sources),
            'cohort_records_containing': len(set().union(*(s['cohort_records'] for s in sources))),
            'current_first_refusal': {f: len(set().union(*(s['first'][f] for s in sources))) for f in flags}})
    # These are unsupported-instruction attributions, distinct from a tree,
    # ABI or control failure merely located at an opcode. Boundary enum value
    # follows SemanticError::kUnsupportedInstruction (semantic_ir.h).
    unsupported_first = [e for e in first if any(
        c['domain'] == 'abcd::analysis::SemanticError' and c['value'] == 1
        for c in e['failure'].get('causes', []))]
    result = {'schema': 1, 'probe_sha256': probe_hash, 'structure_sha256': structure_hash, 'engine_revision': subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
              'classification_scope': 'Per-profile occurrence oracles; variant display is existential and lists verified_profiles, never a transfer to other profile rows',
              'oracle_runs': oracle_runs, 'oracle_occurrences': oracle_index.count,
              'files': files, 'failures': failures, 'first_refusals': first, 'unsupported_first_refusals': unsupported_first, 'unattributed_first_refusals': unattributed, 'rows': variants, 'profile_rows': profile_rows, 'families': family_rollups,
              'classes': {kind: sum(r['classification'] == kind for r in variants) for kind in CLASSES}}
    oracle_index.close()
    (out / 'matrix.json').write_text(json.dumps(result, indent=2)+'\n')
    lines = ['# Measured ISA coverage', '', 'Generated by `tests/isa/coverage_matrix.py`. Oracle layers remain separate. NonExecutable requires a pinned citation and passing constructed-ABC refusal tests.', '',
             f'Engine: `{result["engine_revision"]}`. Files: {len(files)}. Occurrences: {sum(f["instructions"] for f in files)}.', '',
             'Raw lift and record completion do not prove per-instruction source equivalence. Method status is unmeasured unless independently inspected.', '',
             '| Opcode | Family | Occurrences | Measured profiles | Verified profiles | Classification |',
             '|---|---|---:|---|---|---|']
    for r in variants:
        measured = ', '.join(str(p['profile']) for p in r['profiles'] if p['occurrences']) or 'none'
        lines.append(f'| {r["name"]} | {FAMILIES[r["family"]]} | {r["occurrences"]} | {measured} | {','.join(map(str,r['verified_profiles'])) or 'none'} | {r["classification"]} |')
    lines += ['', '## Family impact (distinct records, not summed opcode hits)', '',
              '| Family | Variants observed / total | Occurrences | Cohort records | Located first refusal previous / V4 |',
              '|---|---:|---:|---:|---|']
    for f in sorted(family_rollups, key=lambda x: -x['current_first_refusal']['v4']):
        lines.append(f'| {f["family"]} | {f["observed_variants"]} / {f["variants"]} | {f["occurrences"]} | {f["cohort_records_containing"]} | {f["current_first_refusal"]["previous"]} / {f["current_first_refusal"]["v4"]} |')
    lines += ['', '## Current unsupported instruction first refusals (exploration)', '',
              '| Flags | Input / record | Method / original PC | Decoded opcode |',
              '|---|---|---|---|']
    for e in unsupported_first:
        if e['group'] == 'exploration':
            lines.append(f'| {e["flag"]} | {e["input"]}/{e["record"]} | {e["method"]}@{e["pc"]} | {e["name"]} |')
    (out / 'matrix.md').write_text('\n'.join(lines)+'\n')
    print(json.dumps(result['classes']))

if __name__ == '__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--root',default='.'); parser.add_argument('--output',required=True); parser.add_argument('--limit',type=int)
    run(parser.parse_args())
