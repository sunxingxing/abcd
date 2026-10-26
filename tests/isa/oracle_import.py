"""Bounded disk index for test-produced, profile/occurrence-bound ISA oracles."""
import gzip
import hashlib
import json
from pathlib import Path
import sqlite3

CONTRACTS = {'language_source_v10': 'language_source_v10'}
MAX_JSON_BYTES = 16 * 1024 * 1024
MAX_OCCURRENCE_BYTES = 1024 * 1024

def read_json(path):
    with Path(path).open('rb') as stream:
        data = stream.read(MAX_JSON_BYTES + 1)
    if len(data) > MAX_JSON_BYTES:
        raise ValueError('oracle metadata resource limit')
    return json.loads(data)

ORACLES = ('node', 'ark_round_trip', 'ark_runtime')


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def occurrence_key(row):
    return tuple(row[k] for k in ('sha256', 'profile', 'record', 'method', 'pc'))


class OracleIndex:
    """SQLite provides O(log N) occurrence joins without retaining all rows.
    The cache is bounded to16 MiB; hashing is cached per immutable artifact.
    Invalid/stale evidence is an error, never an empty successful oracle.
    """
    def __init__(self, database, engine_hash, probe_hash, producers):
        self.db = sqlite3.connect(database)
        self.db.execute('PRAGMA cache_size=-16384')
        self.db.execute('DROP TABLE IF EXISTS evidence')
        self.db.execute('CREATE TABLE evidence(sha TEXT,profile INTEGER,record INTEGER,method INTEGER,pc INTEGER,contract TEXT,node TEXT,trip TEXT, PRIMARY KEY(sha,profile,record,method,pc,contract)) WITHOUT ROWID')
        self.engine_hash, self.probe_hash = engine_hash, probe_hash
        self.producers = {str(Path(p).resolve()) for p in producers}
        self.checked = {}
        self.count = 0

    def check_artifact(self, item):
        path, digest = Path(item['path']), item['sha256']
        key = (str(path.resolve()), digest)
        if key not in self.checked:
            if sha(path) != digest:
                raise ValueError('stale oracle artifact: ' + str(path))
            self.checked[key] = True

    def import_file(self, path):
        path = Path(path).resolve()
        evidence = read_json(path)
        if evidence['schema'] != 1 or evidence['cases'] <= 0:
            raise ValueError('oracle schema or executed-case count')
        if str(Path(evidence['producer']['path']).resolve()) not in self.producers:
            raise ValueError('unregistered oracle producer')
        for key in ('writer', 'producer', 'engine', 'probe', 'abc', 'manifest', 'original', 'occurrences'):
            self.check_artifact(evidence[key])
        for dependency in evidence.get('dependencies', []):
            self.check_artifact(dependency)
        if evidence['writer']['sha256'] != sha(Path(__file__).resolve().parents[1]/'support/isa/oracle_evidence.py'):
            raise ValueError('stale oracle writer')
        if evidence['engine']['sha256'] != self.engine_hash or evidence['probe']['sha256'] != self.probe_hash:
            raise ValueError('oracle belongs to stale production/probe binary')
        if evidence['contract'] not in CONTRACTS:
            raise ValueError('unknown oracle recovery contract')
        manifest = read_json(evidence['manifest']['path'])
        if manifest['sha256'] != evidence['abc']['sha256'] or manifest['source_contract'] != evidence['contract']:
            raise ValueError('oracle manifest identity')
        source_modes = {r['entity']: ('script' if r.get('source_language')=='javascript-script' else 'module') for r in manifest['records']}
        complete = {r['entity'] for r in manifest['records'] if r.get('source_status') == 0}
        sources = {}
        correspondences = {}
        for record in evidence['records']:
            if record['record'] not in complete:
                raise ValueError('oracle has incomplete record')
            expected_mode=source_modes[record['record']]
            if record.get('source_mode','module')!=expected_mode:
                raise ValueError('oracle script/module mode mismatch')
            self.check_artifact(record['emitted'])
            sources[record['record']] = record['emitted']
            receipt = record.get('round_trip')
            if receipt:
                self.check_artifact(receipt)
                data = read_json(receipt['path'])
                for name in ('compiler', 'disassembler', 'abc'):
                    self.check_artifact(data[name])
                correspondences[record['record']] = {
                    (e['method'], e['pc']): e['witness'] for e in data['events']}

        with gzip.open(evidence['occurrences']['path'], 'rt') as stream:
            while line := stream.readline(MAX_OCCURRENCE_BYTES + 1):
                if len(line) > MAX_OCCURRENCE_BYTES:
                    raise ValueError('oracle occurrence resource limit')
                row = json.loads(line)
                if row['sha256'] != evidence['abc']['sha256'] or row['record'] not in sources:
                    raise ValueError('oracle occurrence identity')
                if row.get('ark_runtime') is not None:
                    raise ValueError('Ark runtime oracle unavailable')
                values = []
                for kind in ('node', 'ark_round_trip'):
                    oracle = row.get(kind)
                    if oracle is None:
                        values.append(None)
                        continue
                    if oracle.get('status') != 'pass' or oracle.get('skipped'):
                        raise ValueError('skipped/refused oracle cannot pass')
                    if kind == 'ark_round_trip' and oracle['semantic_family'] != row['semantic_family']:
                        raise ValueError('round-trip semantic-family mismatch')
                    if kind == 'ark_round_trip' and correspondences.get(row['record'], {}).get((row['method'], row['pc'])) != oracle:
                        raise ValueError('round-trip event correspondence changed')
                    values.append(json.dumps({'status': 'pass', 'evidence': str(path),
                        'cases': evidence['cases'], 'semantic_family': row['semantic_family'],
                        'opcode': row['opcode'], 'witness': oracle,
                        'exclusions': evidence.get('exclusions', [])}, sort_keys=True))
                key = occurrence_key(row)
                self.db.execute('INSERT INTO evidence VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(sha,profile,record,method,pc,contract) DO UPDATE SET node=COALESCE(excluded.node,evidence.node),trip=COALESCE(excluded.trip,evidence.trip)',
                                (*key, evidence['contract'], *values))
                self.count += 1
        self.db.commit()

    def lookup(self, row):
        result = dict.fromkeys(ORACLES)
        found = self.db.execute('SELECT contract,node,trip FROM evidence WHERE sha=? AND profile=? AND record=? AND method=? AND pc=?', occurrence_key(row))
        for contract, node, trip in found:
            flag = CONTRACTS[contract]
            if row.get('recovery', {}).get(flag, {}).get('record_status') != 0:
                raise ValueError('oracle recovery is not currently complete')
            if row['lift']['status'] != 'ok':
                raise ValueError('oracle occurrence now refuses raw lift')
            for name, value in (('node', node), ('ark_round_trip', trip)):
                if value is None:
                    continue
                item = json.loads(value)
                if item['opcode'] != row['encoding']['opcode'] or item['semantic_family'] != row['lift']['semantic_family']:
                    raise ValueError('oracle occurrence opcode/family changed')
                result[name] = item
        return result

    def close(self):
        self.db.close()
