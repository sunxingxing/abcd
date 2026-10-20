#!/usr/bin/env python3
"""OLD/NEW grouping acceptance. Sequential compilers; no register/CFG normalization.
Usage: grouping_bytecode.py --old OLD_DIR --new NEW_DIR --out SCRATCH_DIR
Directories contain cohort subdirectories (or are one record-export directory).
"""
import argparse, difflib, hashlib, json, os, pathlib, subprocess, sys, re
from enum import Enum
ES2ABC='/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc'
DISASM='/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains/ark_disasm'

def sha(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()

def records(root, include_incomplete=False):
    result={}
    for manifest in sorted(root.rglob('manifest.json')):
        data=json.loads(manifest.read_text())
        if not isinstance(data,dict) or 'records' not in data:continue
        for record in data['records']:
            source=record.get('source') or (record.get('incomplete_source') if include_incomplete else None)
            if source and (record.get('source_status')==0 or include_incomplete) and (manifest.parent/source).exists():
                key=str(manifest.parent.relative_to(root))+'/'+str(record['entity'])
                result[key]=(manifest.parent/source,record)
    return result

def run(command,log):
    try:
        p=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
        log.write_bytes(p.stdout)
        return p.returncode
    except subprocess.TimeoutExpired as e:
        log.write_bytes((e.stdout or b'')+b'\nTIMEOUT\n');return 124

def assembly(path):
    # Default ark_disasm does not emit debug line tables. Preserve ALL literal,
    # annotation, module, handler, register, operand and instruction text. The
    # only omitted line is its informational source-binary filename comment.
    return ''.join(line for line in path.read_text().splitlines(keepends=True)
                   if not line.startswith('# source binary: '))

class DeadWrapperLoad(Enum):
    ZERO_BEFORE_REGISTER_LOAD = 'zero_before_register_load'
    ZERO_BEFORE_BINDING_LOAD = 'zero_before_binding_load'

def wrapper_load_difference(old, new, wrapper_count):
    """Only ldai 0 immediately overwritten by a register/binding load; no label or edge
    can intervene. Every other byte of disassembly must match. Source token
    validation separately restricts input changes to eligible wrapper sites.
    Do not add arbitrary dead-code normalization here.
    """
    a,b=old.splitlines(True),new.splitlines(True);i=j=0;proof=[]
    while i<len(a) and j<len(b):
        if a[i]==b[j]:i+=1;j+=1;continue
        if (a[i].strip()=='ldai 0x0' and i+1<len(a) and
            re.fullmatch(r'\s*(?:lda [va][0-9]+|ldlexvar 0x[0-9a-f]+, 0x[0-9a-f]+|ld(?:external|local)modulevar 0x[0-9a-f]+|tryldglobalbyname 0x[0-9a-f]+, \"[^\"]+\")\s*',a[i+1]) and a[i+1]==b[j]):
            proof.append({'kind':(DeadWrapperLoad.ZERO_BEFORE_REGISTER_LOAD if a[i+1].strip().startswith('lda ') else DeadWrapperLoad.ZERO_BEFORE_BINDING_LOAD).value,'line':i+1});i+=1;continue
        return None
    return proof if i==len(a) and j==len(b) and 0<len(proof)<=wrapper_count else None

def changed_methods(old, new, classes):
    # A report, never a normalization: retain complete function headers/body
    # text. Whole-record equality with the compiled approved witness remains
    # mandatory, including metadata outside these bodies.
    def parts(text):
        return {body.splitlines()[0]:body for body in re.findall(r'(?ms)^\.function .*?^}\n', text)}
    a,b=parts(old),parts(new)
    return [{'method':name,'approved_rewrite_classes':[k for k,v in classes.items() if v]}
            for name in sorted(a.keys()|b.keys()) if a.get(name)!=b.get(name)]

def main():
    p=argparse.ArgumentParser();p.add_argument('--old',type=pathlib.Path,required=True);p.add_argument('--new',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True)
    p.add_argument('--readable-statements',action='store_true');p.add_argument('--negated-equality',action='store_true');p.add_argument('--changed-only',action='store_true');p.add_argument('--include-incomplete',action='store_true');p.add_argument('--braces',action='store_true');p.add_argument('--value-calls',action='store_true');p.add_argument('--es2abc',default=ES2ABC);p.add_argument('--disasm',default=DISASM);a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=False)
    old,new=records(a.old,a.include_incomplete),records(a.new,a.include_incomplete)
    result={'old_complete':sum(v[1]['source_status']==0 for v in old.values()),'new_complete':sum(v[1]['source_status']==0 for v in new.values()),'lost':sorted(old.keys()-new.keys()),'added':sorted(new.keys()-old.keys()),
            'compiler':{'path':a.es2abc,'sha256':sha(a.es2abc)},'disassembler':{'path':a.disasm,'sha256':sha(a.disasm)},'records':[]}
    pairs=[]
    result['status_changes']=[key for key in old.keys()&new.keys() if old[key][1]['source_status']!=new[key][1]['source_status']]
    for index,key in enumerate(sorted(old.keys()&new.keys())):
        before,after=old[key][0].resolve(),new[key][0].resolve()
        if a.changed_only and before.read_bytes()==after.read_bytes():continue
        dest=a.out/str(len(pairs));dest.mkdir(exist_ok=False)
        for side in ('old','new'):(dest/side).mkdir(exist_ok=False)
        extension='.ts' if before.suffix=='.ets' else '.js'
        pair={'id':key,'old':str(before),'new':str(after),'adapted_old':str((dest/'old'/('input'+extension)).resolve()),'adapted_new':str((dest/'new'/('input'+extension)).resolve())}
        if before.suffix=='.ets':pair['adapted_node']=str((dest/'node-shadow.mjs').resolve())
        if a.readable_statements:
            (dest/'readable').mkdir();pair['adapted_readable']=str((dest/'readable'/('input'+extension)).resolve())
        if a.negated_equality:
            (dest/'witness').mkdir();pair['adapted_negated']=str((dest/'witness'/('input'+extension)).resolve())
        pairs.append(pair)
    pairfile=a.out/'pairs.json';pairfile.write_text(json.dumps(pairs,indent=2))
    tokens=a.out/'tokens.json'
    command=['node','--expose-internals',str(pathlib.Path(__file__).with_name('grouping_pairs.mjs')),str(pairfile),str(tokens)]
    if a.readable_statements:command+=['--readable-statements']
    if a.negated_equality:command+=['--negated-equality']
    if a.value_calls:command+=['--value-calls']
    if a.braces:command+=['--braces']
    if run(command,a.out/'tokens.log'):raise RuntimeError('token/adaptation tooling failed')
    token_results=json.loads(tokens.read_text());result['tokens']={s:sum(x['status']==s for x in token_results) for s in ('pass','fail','unverified')}
    if a.readable_statements:result['readable_counts']={k:sum(x.get('readable',{}).get(k,0) for x in token_results) for k in ('tdzGuard','emptyElse','invertedEmpty','switchBreak','switchLabel','switchValue')}
    result['negated_equalities']=sum(x.get('negated',0) for x in token_results)
    result['changed_files']=len(pairs) if a.changed_only else sum(pathlib.Path(x['old']).read_bytes()!=pathlib.Path(x['new']).read_bytes() for x in pairs)
    result['removed_pairs']=sum(x['removed'] for x in token_results)
    if a.braces:result['brace_counts']={k:sum(x.get(k,0) for x in token_results) for k in ('elseIf','flattened')}
    result['text_counts']={k:sum(x[k] for x in token_results) for k in ('oldDouble','newDouble','oldObject','newObject')}
    result['bytecode']={mode:{'pass':0,'allowed':0,'differ':0,'unverified':0} for mode in ('default','O0')}
    result['node']={'pass':0,'fail':0,'unverified':0}
    examples=[]
    for index,pair in enumerate(pairs):
        dest=a.out/str(index);row={'id':pair['id'],'token':token_results[index],'bytecode':{}}
        row['node']='pass' if run(['node','--check',pair.get('adapted_node',pair['new'])],dest/'node.log')==0 else 'fail'
        if 'adapted_node' in pair:row['node_type_shadow']=True
        result['node'][row['node']]+=1
        before=pathlib.Path(pair['old']).read_text();after=pathlib.Path(pair['new']).read_text()
        if len(examples)<10:
            for x,y in zip(before.splitlines(),after.splitlines()):
                if x!=y:examples.append({'id':pair['id'],'before':x,'after':y});break
        for mode in ('default','O0'):
            rc={};cmds={}
            for side in ('old','new'):
                source=pathlib.Path(pair['adapted_'+side]);abc=dest/side/(mode+'.abc');pa=dest/side/(mode+'.pa')
                # Explicit record/source identity is identical on both sides.
                cmd=[a.es2abc,'--module','--merge-abc','--record-name=grouping_acceptance','--source-file=input'+source.suffix,
                     '--extension='+('ts' if source.suffix=='.ts' else 'js'),'--output='+str(abc)]
                if mode=='O0':cmd+=['--opt-level=0']
                cmd+=[str(source)];cmds[side]=cmd
                rc[side]=run(cmd,dest/side/(mode+'.compile.log'))
                if rc[side]==0:rc[side]=run([a.disasm,str(abc),str(pa)],dest/side/(mode+'.disasm.log'))
            if rc['old'] or rc['new']:
                status='unverified' if rc['old'] and rc['new'] else 'differ'
            else:
                x=assembly(dest/'old'/(mode+'.pa'));y=assembly(dest/'new'/(mode+'.pa'))
                status='pass' if x==y else 'differ'
                if status=='differ' and a.readable_statements and token_results[index]['status']=='pass':
                    # Compile the independent AST-derived OLD witness. Only
                    # approved statement edits are present; no disassembly
                    # normalization or matching by changed register numbers.
                    source=pathlib.Path(pair['adapted_readable']);abc=dest/'readable'/(mode+'.abc');pa=dest/'readable'/(mode+'.pa')
                    cmd=list(cmds['old']);cmd[cmd.index(next(c for c in cmd if c.startswith('--output=')))]= '--output='+str(abc);cmd[-1]=str(source)
                    witness_rc=run(cmd,dest/'readable'/(mode+'.compile.log'))
                    if witness_rc==0:witness_rc=run([a.disasm,str(abc),str(pa)],dest/'readable'/(mode+'.disasm.log'))
                    if witness_rc==0 and assembly(pa)==y:
                        status='allowed';row.setdefault('readable_witness',{})[mode]={'classes':token_results[index].get('readable',{}),'command':cmd,'exact_assembly_match':True,'changed_methods':changed_methods(x,y,token_results[index].get('readable',{}))}
                        (dest/(mode+'.readable.diff')).write_text(''.join(difflib.unified_diff(x.splitlines(True),y.splitlines(True),fromfile='old',tofile='new')))
                if status=='differ' and a.negated_equality and token_results[index]['status']=='pass' and token_results[index].get('negated',0):
                    # Compile ONLY the independently AST-derived approved rewrite
                    # on OLD. No register/branch/handler normalization: NEW must
                    # exactly match this third compilation, including metadata.
                    source=pathlib.Path(pair['adapted_negated']);abc=dest/'witness'/(mode+'.abc');pa=dest/'witness'/(mode+'.pa')
                    cmd=list(cmds['old']);cmd[cmd.index(next(c for c in cmd if c.startswith('--output=')))]= '--output='+str(abc);cmd[-1]=str(source)
                    witness_rc=run(cmd,dest/'witness'/(mode+'.compile.log'))
                    if witness_rc==0:witness_rc=run([a.disasm,str(abc),str(pa)],dest/'witness'/(mode+'.disasm.log'))
                    if witness_rc==0 and assembly(pa)==y:
                        status='allowed';row.setdefault('negated_equality_witness',{})[mode]={'count':token_results[index]['negated'],'command':cmd,'exact_assembly_match':True}
                if status=='differ' and a.value_calls and token_results[index]['status']=='pass':
                    proof=wrapper_load_difference(x,y,token_results[index]['removed'])
                    if proof:
                        status='allowed';row.setdefault('allowed_loads',{})[mode]=proof
                if status=='differ' and a.value_calls and token_results[index]['status']=='unverified':
                    # No grammar witness for ArkTS: never count this as allowed.
                    if wrapper_load_difference(x,y,len(x.splitlines())):status='unverified'
                if status=='allowed' and 'negated_equality_witness' in row:(dest/(mode+'.negated-equality.diff')).write_text(''.join(difflib.unified_diff(x.splitlines(True),y.splitlines(True),fromfile='old',tofile='new')))
                if status=='differ':(dest/(mode+'.diff')).write_text(''.join(difflib.unified_diff(x.splitlines(True),y.splitlines(True),fromfile='old',tofile='new')))
            result['bytecode'][mode][status]+=1
            row['bytecode'][mode]={'status':status,'exit':rc,'commands':cmds}
        result['records'].append(row)
        (a.out/'report.json').write_text(json.dumps(result,indent=2))
        if index%50==0:print(index+1,len(pairs),result['bytecode'],flush=True)
    result['examples']=examples;(a.out/'report.json').write_text(json.dumps(result,indent=2))
    print(json.dumps({k:v for k,v in result.items() if k not in ('records','examples')},indent=2))
    return int(bool(result['status_changes'] or result['lost'] or result['added'] or result['tokens']['fail'] or result['node']['fail'] or any(v['differ'] for v in result['bytecode'].values())))
if __name__=='__main__':sys.exit(main())
