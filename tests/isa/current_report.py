#!/usr/bin/env python3
"""Reproduce the reviewed current-variant inventory from measured evidence."""
import argparse
import collections
import gzip
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]

def family(name):
    if 'SENDABLE' in name:return 'Sendable storage/module domains'
    if 'PATCHVAR' in name:return 'Patch environment'
    if name.startswith('THROW_'):return 'Abrupt protocol checks'
    if any(x in name for x in ('GENERATOR','RESUMEMODE','ASYNCFUNCTIONAWAIT')):return 'Generator/async protocols'
    if 'ITERATOR' in name:return 'Iterator protocols'
    if 'PRIVATE' in name or name.startswith(('TESTIN','CALLRUNTIME_CALLINIT')):return 'Private/class initialization'
    if 'MODULE' in name:return 'Module namespaces/lazy reads'
    if name.startswith(('LDTHISBY','STTHISBY','LDSUPER','STSUPER')):return 'This/super references'
    if 'OBJBYINDEX' in name:return 'Indexed property access'
    if any(x in name for x in ('STOWNBY','STOBJBYVALUE','DEFINEFIELDBYVALUE')):return 'Own definitions/computed Set'
    if any(x in name for x in ('LEXENV','LEXVAR','COPYRESTARGS')):return 'Lexical environment/rest'
    if 'GLOBAL' in name:return 'Global environment'
    if any(x in name for x in ('GETTEMPLATE','CREATEREGEXP','COPYDATA','WITHPROTO','WITHEXCLUDED')):return 'Literal/object protocols'
    if any(x in name for x in ('APPLY','CALL','NEWOBJRANGE')):return 'Call/construct argument protocols'
    if name.startswith('DEFINEMETHOD'):return 'Method creation'
    if name in ('LDTHIS','LDNEWTARGET','LDFUNCTION','LDSYMBOL','GETUNMAPPEDARGS'):return 'Frame/intrinsic identities'
    if name.startswith(('JMP','JEQZ','JNEZ')) or name in ('NOP','DEBUGGER'):return 'Control/debugger'
    return 'Primitive operations'
def link(ref):return f"[{Path(ref['path']).name}:{ref['line']}](../../../{ref['path']}#L{ref['line']})"
def main():
    p=argparse.ArgumentParser();p.add_argument('--coverage',type=Path,required=True);args=p.parse_args()
    specs=json.loads((ROOT/'tests/isa/current_variants.json').read_text())['variants']
    manifest=json.loads((ROOT/'tests/fixtures/isa/current/manifest.json').read_text())
    entries={e['opcode']:e for e in manifest['fixtures'] if e['origin']=='constructed'}
    groups={s['opcode']:family(s['name']) for s in specs};seen=collections.defaultdict(set)
    for path in sorted((args.coverage/'occurrences/exploration').glob('*.jsonl.gz')):
        with gzip.open(path,'rt') as stream:
            for line in stream:
                r=json.loads(line)
                if r['type']=='instruction' and r['encoding']['opcode'] in groups:seen[groups[r['encoding']['opcode']]].add((r['sha256'],r['record']))
    matrix=json.loads((args.coverage/'matrix.json').read_text());first=collections.defaultdict(lambda:collections.Counter())
    for e in matrix['first_refusals']:
        if e['group']=='exploration' and e['opcode'] in groups:first[groups[e['opcode']]][e['flag']]+=1
    rows=['## Family impact of this fixed baseline selection','', '| Family | Variants | Cohort records containing | Located first refusal previous / V4 |','|---|---:|---:|---:|']
    for f in sorted(set(groups.values()),key=lambda f:(-first[f]['v4'],-len(seen[f]),f)):
        rows.append(f"| {f} | {sum(x==f for x in groups.values())} | {len(seen[f])} | {first[f]['previous']} / {first[f]['v4']} |")
    rows+=['','The first-refusal column is a location attribution, not an opcode-support claim or a completion promise. Containment is a distinct-record union, not a sum across variants.','', '## Per-variant evidence','', '| Variant / backlog family | C++ interpreter; other located paths | Dynamic emitter reference | Actual O0 / default probe | Constructed raw lift | Cohort containing; first old / V4 |','|---|---|---|---|---|---|']
    for s in specs:
        ev=s['evidence'];emit=ev['frontend_emit_sites'];rt=ev['runtime'];entry=entries[s['opcode']]
        observations=[]
        for opt in ('o0','default'):
            a=[x['source'] for x in manifest['attempts'] if x['exit']==0 and x['path'].endswith('-'+opt+'.abc') and s['opcode'] in x['opcodes']]
            observations.append(', '.join(a[:2])+(' (+more)' if len(a)>2 else '') if a else 'not observed')
        lift=entry['lift'];status=lift.get('semantic_family','').replace('abcd::analysis::','') if lift['status']=='ok' else 'SemanticError::kUnsupportedInstruction'
        impact=s['impact'];ct=sum(p['cohort_records_containing'] for p in impact);a=sum(p['current_first_refusal']['previous'] for p in impact);b=sum(p['current_first_refusal']['v4'] for p in impact)
        runtime=ev['interpreter']+': '+link(rt[0])+'; '+', '.join(link(r) for r in rt[1:])
        emitter=', '.join(link(r) for r in emit) if emit else 'No direct emitter located; see path notes'
        rows.append(f"| `{s['name']}`<br>{groups[s['opcode']]} | {runtime} | {emitter} | {' / '.join(observations)} | {status} | {ct}; {a} / {b} |")
    out=ROOT/'docs/design/source-recovery/isa-current-evidence.md';old=out.read_text().split('<!-- GENERATED CURRENT EVIDENCE -->')[0]
    out.write_text(old+'<!-- GENERATED CURRENT EVIDENCE -->\n\n'+'\n'.join(rows)+'\n')
if __name__=='__main__':main()
