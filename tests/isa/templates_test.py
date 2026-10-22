#!/usr/bin/env python3
"""Private template arrays and native tag event correspondence, test-only."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path[:0]=[str(ROOT/'tests/isa'),str(ROOT/'tests/support/abc'),str(ROOT/'tests/support/isa')]
from builder import Module,FunctionCode,Profile,build
from native_frame_test import catalog,encode
from oracle_evidence import sha,record_node
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/templates'
SDK=Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony')
COMPILER=SDK/'ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc';DISASM=SDK/'toolchains/ark_disasm'
EXCLUSIONS=('V7: raw/cooked indexed descriptor enumerability','V7: template identity caching across repeated evaluations')
def specimen(kind,profile,raw='a',cooked='a'):
 c=catalog();op=lambda n,a=():encode(c[n],a)
 code=[op('CREATEEMPTYARRAY_IMM8',(0,)),op('STA_V8',(0,)),op('CREATEEMPTYARRAY_IMM8',(1,)),op('STA_V8',(1,))]
 for i,(r,s) in enumerate([(raw,cooked),('b','b')]):
  for array,pool in [(0,2+i*2),(1,3+i*2)]:code += [op('LDA_STR_ID16',(pool,)),op('STOWNBYINDEX_IMM8_V8_IMM16',(2,array,i))]
 code += [op('CREATEEMPTYARRAY_IMM8',(3,)),op('STA_V8',(2,)),op('LDA_V8',(0,)),op('STOWNBYINDEX_IMM8_V8_IMM16',(4,2,0)),op('LDA_V8',(1,)),op('STOWNBYINDEX_IMM8_V8_IMM16',(5,2,1)),op('LDA_V8',(2,)),op(kind,(300 if 'IMM16' in kind else 6,)),op('STA_V8',(3,)),op('LDA_V8',(8,)),op('CALLARGS2_IMM8_V8_V8',(7,3,9)),op('RETURN')]
 child=FunctionCode('f',tuple(code),registers=5,arguments=5)
 return Module((op('DEFINEFUNC_IMM8_ID16_IMM8',(0,1,2)),op('STMODULEVAR_IMM8',(0,)),op('RETURNUNDEFINED')),functions=(child,),exports=('f',),pool_strings=(raw,cooked,'b','b'),profile=profile)
def probe(abc):return [r for line in subprocess.check_output([BUILD/'isa_coverage_probe',abc],text=True,stderr=subprocess.DEVNULL).splitlines() if (r:=json.loads(line))['type']=='instruction']
def compile_source(source,abc,opts=()):
 cmd=[str(COMPILER),'--module','--merge-abc','--extension=js','--record-name=isaTemplates','--source-file='+source.name,*opts,'--output='+str(abc),str(source)];subprocess.run(cmd,check=True,cwd=ROOT,capture_output=True);return cmd
def generate():
 entries=[];(FIX/'constructed.js').write_text('export function f(tag,x){return tag`a${x}b`;}\n')
 wide=FIX/'wide.js';wide.write_text('export function wide(o,tag,x){\n'+''.join(f'o.p{i};\n' for i in range(140))+'return tag`a${x}b`;}\n')
 for source in [FIX/'templates.js',wide]:
  for mode,opts in [('default',[]),('o0',['--opt-level=0'])]:
   abc=FIX/(source.stem+'-'+mode+'.abc');cmd=compile_source(source,abc,opts);rows=probe(abc);entries.append(dict(path=abc.name,sha256=sha(abc),source=source.name,source_sha256=sha(source),origin='es2abc',command=cmd,opcodes=sorted({r['encoding']['opcode'] for r in rows})))
 for kind in ['GETTEMPLATEOBJECT_IMM8','GETTEMPLATEOBJECT_IMM16']:
  for profile in [Profile.DYNAMIC1206,Profile.DYNAMIC2400]:
   abc=FIX/(kind.lower()+'-'+profile.name.lower()+'.abc');abc.write_bytes(build(specimen(kind,profile))[0]);entries.append(dict(path=abc.name,sha256=sha(abc),source='constructed.js',source_sha256=sha(FIX/'constructed.js'),origin='constructed',kind=kind,profile=profile.name,opcodes=sorted({r['encoding']['opcode'] for r in probe(abc)})))
 (FIX/'manifest.json').write_text(json.dumps(dict(compiler_sha256=sha(COMPILER),disassembler_sha256=sha(DISASM),fixtures=entries),indent=2)+'\n')
 p=ROOT/'tests/isa/coverage_manifest.json';d=json.loads(p.read_text());prefix='tests/fixtures/isa/templates/';d['fixtures']=[e for e in d['fixtures'] if not e['path'].startswith(prefix)]
 for e in entries:d['fixtures'].append(dict(path=prefix+e['path'],sha256=e['sha256'],opcodes=e['opcodes'],classification='Unmeasured',oracles={},note='Private template construction; only occurrence-bound executed evidence promotes. V7 indexed descriptors and caching exclusions.'))
 d['oracle_producers']=[e for e in d['oracle_producers'] if e['test']!='isa_templates_source_test'];d['oracle_producers'].append(dict(test='isa_templates_source_test',producer='tests/isa/templates_test.py'));p.write_text(json.dumps(d,indent=2)+'\n')
def recover(abc,out):
 subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True);return next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2)
class Templates(unittest.TestCase):
 def test_complete_oracles(self):
  registry=json.loads((FIX/'manifest.json').read_text());self.assertEqual(sha(COMPILER),registry['compiler_sha256']);self.assertEqual(sha(DISASM),registry['disassembler_sha256'])
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,e in enumerate(registry['fixtures']):
    with self.subTest(fixture=e['path']):
     abc=FIX/e['path'];source=FIX/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
     if e['origin']=='constructed':self.assertEqual(abc.read_bytes(),build(specimen(e['kind'],Profile[e['profile']]))[0])
     else:
      cmd=[('--output='+str(root/'check.abc')) if a.startswith('--output=') else a for a in e['command']];subprocess.run(cmd,check=True,cwd=ROOT,capture_output=True);self.assertEqual(sha(root/'check.abc'),e['sha256'])
     subprocess.run([DISASM,abc,root/'decoded.pa'],check=True,capture_output=True)
     rows=probe(abc);self.assertTrue(all(r['lift']['status']=='ok' for r in rows));self.assertTrue(any(r['encoding']['opcode'] in [catalog()['GETTEMPLATEOBJECT_IMM8']['opcode'],catalog()['GETTEMPLATEOBJECT_IMM16']['opcode']] for r in rows))

     if e['path'].startswith('wide-'):self.assertIn(catalog()['GETTEMPLATEOBJECT_IMM16']['opcode'],{r['encoding']['opcode'] for r in rows})
     out=root/str(i);r=recover(abc,out);self.assertEqual(r['source_status'],0,r);emitted=out/r['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
     result=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/isa_templates.mjs',source,emitted],text=True));self.assertGreater(result['cases'],0);record_node(abc,out,source,__file__,result['cases'],BUILD/'abc_structure',exclusions=EXCLUSIONS)
 def test_scale_records(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for count in [200,201,400,800]:
    source=root/f's{count}.js';source.write_text('export function f(tag,x){'+''.join('tag`a${x}b`;' for _ in range(count))+'return tag`'+'a'*count+'${x}q`;}\n');abc=root/f's{count}.abc';compile_source(source,abc);out=root/str(count);r=recover(abc,out);self.assertEqual(r['source_status'],0,r);emitted=out/r['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
    code="import assert from 'node:assert/strict';const a=await import(process.argv[1]),b=await import(process.argv[2]);const run=m=>{const trace=[];const tag=(s,...v)=>{const q=[[...s],[...s.raw],v];trace.push(q);return q;};return [m.f(tag,9),trace];};assert.deepEqual(run(a),run(b));";subprocess.run(['node','--input-type=module','-e',code,'data:text/javascript;base64,'+__import__('base64').b64encode(source.read_bytes()).decode(),emitted.as_uri()],check=True,capture_output=True)
 def test_refusals(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   # Raw/cooked mismatch and no equivalent raw source token remain refused.
   for i,(raw,cooked) in enumerate([('a','z'),('`','`'),('${x}','${x}'),('\\xG','x')]):
    abc=root/f'{i}.abc';abc.write_bytes(build(specimen('GETTEMPLATEOBJECT_IMM8',Profile.DYNAMIC2400,raw,cooked))[0]);self.assertNotEqual(recover(abc,root/str(i))['source_status'],0)
   abc=FIX/'templates-default.abc';out=root/'off';subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-numeric','--language-constructors'],check=True,capture_output=True);r=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2);self.assertNotEqual(r['source_status'],0)
if __name__=='__main__':
 if '--generate' in sys.argv:generate()
 else:unittest.main()
