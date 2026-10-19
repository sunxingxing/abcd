#!/usr/bin/env python3
"""OLD/NEW grouping acceptance. Sequential compilers; no register/CFG normalization.
Usage: grouping_bytecode.py --old OLD_DIR --new NEW_DIR --out SCRATCH_DIR
Directories contain cohort subdirectories (or are one record-export directory).
"""
import argparse, difflib, hashlib, json, os, pathlib, subprocess, sys
ES2ABC='/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc'
DISASM='/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains/ark_disasm'

def sha(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()

def records(root):
    result={}
    for manifest in sorted(root.rglob('manifest.json')):
        data=json.loads(manifest.read_text())
        if not isinstance(data,dict) or 'records' not in data:continue
        for record in data['records']:
            if record.get('source_status')==0:
                key=str(manifest.parent.relative_to(root))+'/'+str(record['entity'])
                result[key]=(manifest.parent/record['source'],record)
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

def main():
    p=argparse.ArgumentParser();p.add_argument('--old',type=pathlib.Path,required=True);p.add_argument('--new',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True)
    p.add_argument('--es2abc',default=ES2ABC);p.add_argument('--disasm',default=DISASM);a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=False)
    old,new=records(a.old),records(a.new)
    result={'old_complete':len(old),'new_complete':len(new),'lost':sorted(old.keys()-new.keys()),'added':sorted(new.keys()-old.keys()),
            'compiler':{'path':a.es2abc,'sha256':sha(a.es2abc)},'disassembler':{'path':a.disasm,'sha256':sha(a.disasm)},'records':[]}
    pairs=[]
    for index,key in enumerate(sorted(old.keys()&new.keys())):
        before,after=old[key][0].resolve(),new[key][0].resolve()
        dest=a.out/str(index);dest.mkdir()
        for side in ('old','new'):(dest/side).mkdir()
        extension='.ts' if before.suffix=='.ets' else '.js'
        pair={'id':key,'old':str(before),'new':str(after),'adapted_old':str((dest/'old'/('input'+extension)).resolve()),'adapted_new':str((dest/'new'/('input'+extension)).resolve())}
        pairs.append(pair)
    pairfile=a.out/'pairs.json';pairfile.write_text(json.dumps(pairs,indent=2))
    tokens=a.out/'tokens.json'
    command=['node','--expose-internals',str(pathlib.Path(__file__).with_name('grouping_pairs.mjs')),str(pairfile),str(tokens)]
    if run(command,a.out/'tokens.log'):raise RuntimeError('token/adaptation tooling failed')
    token_results=json.loads(tokens.read_text());result['tokens']={s:sum(x['status']==s for x in token_results) for s in ('pass','fail','unverified')}
    result['removed_pairs']=sum(x['removed'] for x in token_results)
    result['text_counts']={k:sum(x[k] for x in token_results) for k in ('oldDouble','newDouble','oldObject','newObject')}
    result['bytecode']={mode:{'pass':0,'differ':0,'unverified':0} for mode in ('default','O0')}
    result['node']={'pass':0,'fail':0,'unverified':0}
    examples=[]
    for index,pair in enumerate(pairs):
        dest=a.out/str(index);row={'id':pair['id'],'token':token_results[index],'bytecode':{}}
        if pathlib.Path(pair['new']).suffix=='.ets': row['node']='unverified'
        else:row['node']='pass' if run(['node','--check',pair['new']],dest/'node.log')==0 else 'fail'
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
                if status=='differ':(dest/(mode+'.diff')).write_text(''.join(difflib.unified_diff(x.splitlines(True),y.splitlines(True),fromfile='old',tofile='new')))
            result['bytecode'][mode][status]+=1
            row['bytecode'][mode]={'status':status,'exit':rc,'commands':cmds}
        result['records'].append(row)
        (a.out/'report.json').write_text(json.dumps(result,indent=2))
        if index%50==0:print(index+1,len(pairs),result['bytecode'],flush=True)
    result['examples']=examples;(a.out/'report.json').write_text(json.dumps(result,indent=2))
    print(json.dumps({k:v for k,v in result.items() if k not in ('records','examples')},indent=2))
    return int(bool(result['lost'] or result['added'] or result['tokens']['fail'] or result['node']['fail'] or any(v['differ'] for v in result['bytecode'].values())))
if __name__=='__main__':sys.exit(main())
