#!/usr/bin/env python3
"""Pinned RegExp literal operations. Source oracles never enter the engine."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path[:0]=[str(ROOT/'tests/isa'),str(ROOT/'tests/support/abc'),str(ROOT/'tests/support/isa')]
from builder import Module,FunctionCode,Profile,Instruction,Label,build
from native_frame_test import catalog,encode
from oracle_evidence import sha,record_node
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/regexp'
SDK=Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony')
COMPILER=SDK/'ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc';DISASM=SDK/'toolchains/ark_disasm'
# Explicit runtime bits, independently paired with source flag letters.
PATTERNS=[('basic','a+b',0,'a+b',''),('empty','',0,'(?:)',''),('slash','a/b',0,r'a\/b',''),('escaped',r'a\/b',0,r'a\/b',''),('inClass','[/]',0,r'[\/]',''),('unicode',r'\u0061\u{62}',16,r'\u0061\u{62}','u'),('escapedLines',r'\n\r\u2028\u2029',0,r'\n\r\u2028\u2029',''),('groups',r'^(?:a|b)+(?=c)c$',0,r'^(?:a|b)+(?=c)c$',''),('globalFlag','a',1,'a','g'),('ignoreFlag','a',2,'a','i'),('multiFlag','^a',4,'^a','m'),('dotFlag','a.b',8,'a.b','s'),('unicodeFlag','a',16,'a','u'),('stickyFlag','a',32,'a','y'),('indicesFlag','a',64,'a','d'),('allFlags','a.b',127,'a.b','dgimsuy')]
def loop_specimen(profile):
 c=catalog();op=lambda n,a=():encode(c[n],a)
 def branch(n,l):return Instruction(c[n]['opcode'],target=l,delta_width=1)
 start,end=Label(0),Label(1)
 code=(op('LDAI_IMM32',(0,)),op('STA_V8',(0,)),op('STA_V8',(1,)),start,op('LDAI_IMM32',(3,)),op('LESS_IMM8_V8',(0,0)),branch('JEQZ_IMM8',end),op('CREATEREGEXPWITHLITERAL_IMM16_ID16_IMM8',(300,2,1)),op('STA_V8',(2,)),op('LDOBJBYNAME_IMM8_ID16',(1,3)),op('ADD2_IMM8_V8',(0,1)),op('STA_V8',(1,)),op('LDAI_IMM32',(7,)),op('STOBJBYNAME_IMM8_ID16_V8',(2,3,2)),op('LDA_V8',(0,)),op('INC_IMM8',(0,)),op('STA_V8',(0,)),branch('JMP_IMM8',start),end,op('LDA_V8',(1,)),op('RETURN'))
 return Module((op('DEFINEFUNC_IMM8_ID16_IMM8',(0,1,0)),op('STMODULEVAR_IMM8',(0,)),op('RETURNUNDEFINED')),functions=(FunctionCode('loop',code,registers=3),),exports=('loop',),pool_strings=('a','lastIndex'),profile=profile)

def specimen(kind,profile,patterns=PATTERNS):
 if kind=='loop':return loop_specimen(profile)
 c=catalog();op=lambda n,args=():encode(c[n],args);count=len(patterns)
 functions=tuple(FunctionCode(name,(op(kind,(300 if 'IMM16_ID' in kind else 0,count+1+i,flags)),op('RETURN'))) for i,(name,pattern,flags,body,letters) in enumerate(patterns))
 entry=tuple(item for i in range(count) for item in [op('DEFINEFUNC_IMM8_ID16_IMM8',(i,i+1,0)),op('STMODULEVAR_IMM8',(i,))])+(op('RETURNUNDEFINED'),)
 return Module(entry,profile=profile,functions=functions,exports=tuple(p[0] for p in patterns),pool_strings=tuple(p[1] for p in patterns))
def probe(abc):return [r for line in subprocess.check_output([BUILD/'isa_coverage_probe',abc],text=True,stderr=subprocess.DEVNULL).splitlines() if (r:=json.loads(line))['type']=='instruction']
def generate():
 entries=[];FIX.mkdir(exist_ok=True)
 (FIX/'constructed.js').write_text(''.join('export function '+n+'(){return /'+body+'/'+letters+';}\n' for n,p,f,body,letters in PATTERNS))
 for kind in ['CREATEREGEXPWITHLITERAL_IMM8_ID16_IMM8','CREATEREGEXPWITHLITERAL_IMM16_ID16_IMM8']:
  for profile in [Profile.DYNAMIC1206,Profile.DYNAMIC2400]:
   abc=FIX/(kind.lower()+'-'+profile.name.lower()+'.abc');abc.write_bytes(build(specimen(kind,profile))[0]);entries.append(dict(path=abc.name,sha256=sha(abc),source='constructed.js',source_sha256=sha(FIX/'constructed.js'),origin='constructed',kind=kind,profile=profile.name,opcodes=[catalog()[kind]['opcode']]))
 loop=FIX/'loop.js';loop.write_text('export function loop(){let sum=0;for(let i=0;i<3;i++){const r=/a/g;sum+=r.lastIndex;r.lastIndex=7;}return sum;}\n')
 for profile in [Profile.DYNAMIC1206,Profile.DYNAMIC2400]:
  abc=FIX/('loop-'+profile.name.lower()+'.abc');abc.write_bytes(build(loop_specimen(profile))[0]);entries.append(dict(path=abc.name,sha256=sha(abc),source=loop.name,source_sha256=sha(loop),origin='constructed',kind='loop',profile=profile.name,opcodes=[catalog()['CREATEREGEXPWITHLITERAL_IMM16_ID16_IMM8']['opcode']]))
 source=FIX/'sdk.js';source.write_text((FIX/'constructed.js').read_text()+'''export function loop(){const values=[];for(let i=0;i<3;i++){const r=/a/g;values.push([r.lastIndex,r.test("a"),r.lastIndex]);}return values;}
export function wide(o){\n'''+''.join(f'o.p{i};\n' for i in range(140))+'''return /a/g;}
''')
 for mode,opts in [('default',[]),('o0',['--opt-level=0'])]:
  abc=FIX/('sdk-'+mode+'.abc');cmd=[str(COMPILER),'--module','--merge-abc','--extension=js','--record-name=isaRegExp','--source-file=sdk.js',*opts,'--output='+str(abc.relative_to(ROOT)),str(source.relative_to(ROOT))];subprocess.run(cmd,check=True,cwd=ROOT);entries.append(dict(path=abc.name,sha256=sha(abc),source=source.name,source_sha256=sha(source),origin='es2abc',command=cmd,opcodes=sorted({r['encoding']['opcode'] for r in probe(abc)})))
 (FIX/'manifest.json').write_text(json.dumps(dict(compiler_sha256=sha(COMPILER),disassembler_sha256=sha(DISASM),fixtures=entries),indent=2)+'\n')
 p=ROOT/'tests/isa/coverage_manifest.json';d=json.loads(p.read_text());prefix='tests/fixtures/isa/regexp/';d['fixtures']=[e for e in d['fixtures'] if not e['path'].startswith(prefix)]
 for e in entries:d['fixtures'].append(dict(path=prefix+e['path'],sha256=e['sha256'],opcodes=e['opcodes'],classification='Unmeasured',oracles={},note='RegExp runtime raw bits; only executed occurrence evidence promotes. SDK lowers literals to global RegExp construction.'))
 d['oracle_producers']=[e for e in d['oracle_producers'] if e['test']!='isa_regexp_source_test'];d['oracle_producers'].append(dict(test='isa_regexp_source_test',producer='tests/isa/regexp_test.py'));p.write_text(json.dumps(d,indent=2)+'\n')
class RegExp(unittest.TestCase):
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
     rows=probe(abc);self.assertTrue(all(r['lift']['status']=='ok' for r in rows))
     if e['origin']=='es2abc':self.assertFalse(any(r['encoding']['opcode'] in [catalog()[n]['opcode'] for n in ['CREATEREGEXPWITHLITERAL_IMM8_ID16_IMM8','CREATEREGEXPWITHLITERAL_IMM16_ID16_IMM8']] for r in rows))
     out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True);record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2);self.assertEqual(record['source_status'],0,record)
     emitted=out/record['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True);result=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/isa_regexp.mjs',source,emitted,e['origin']],text=True));record_node(abc,out,source,__file__,result['cases'],BUILD/'abc_structure')
 def test_scale_records(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);c=catalog();op=lambda n,args=():encode(c[n],args)
   for count in [200,201,400,800]:
    child=FunctionCode('f',tuple(op('CREATEREGEXPWITHLITERAL_IMM16_ID16_IMM8',(300,2,1)) for _ in range(count))+(op('RETURN'),))
    module=Module((op('DEFINEFUNC_IMM8_ID16_IMM8',(0,1,0)),op('STMODULEVAR_IMM8',(0,)),op('RETURNUNDEFINED')),profile=Profile.DYNAMIC2400,functions=(child,),exports=('f',),pool_strings=('a',))
    abc=root/f'{count}.abc';abc.write_bytes(build(module)[0]);out=root/str(count);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True);record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2);self.assertEqual(record['source_status'],0,record);subprocess.run(['node','--check',out/record['source']],check=True,capture_output=True);subprocess.run(['node','--input-type=module','-e',"import assert from 'node:assert/strict'; const {f}=await import(process.argv[1]);const a=f(),b=f();assert.notEqual(a,b);a.lastIndex=7;assert.equal(b.lastIndex,0);assert.equal(a.source,'a');",(out/record['source']).as_uri()],check=True,capture_output=True)
 def test_refusals(self):
  patterns=[('invalid','(',0,'',''),('range','[z-a]',0,'',''),('unproven',r'\p{L}',16,'',''),('source','\n',0,'',''),('flags','a',128,'','')]
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,p in enumerate(patterns):
    abc=root/f'{i}.abc';abc.write_bytes(build(specimen('CREATEREGEXPWITHLITERAL_IMM8_ID16_IMM8',Profile.DYNAMIC2400,[p]))[0]);out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True);record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2);self.assertNotEqual(record['source_status'],0)
if __name__=='__main__':
 if '--generate' in sys.argv:generate()
 else:unittest.main()
