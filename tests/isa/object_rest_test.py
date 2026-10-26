#!/usr/bin/env python3
"""V7 object-rest construction/guard proof; originals are evaluation-only."""
import json, subprocess, sys, tempfile, unittest
from pathlib import Path
from dataclasses import replace
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tests/isa'),str(ROOT/'tests/support/isa'),str(ROOT/'tests/support/abc')]
from oracle_evidence import sha,record_node,round_trip
from native_frame_test import catalog,encode
from builder import Module,FunctionCode,Profile,Label,Instruction,build
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/object_rest'
SDK=Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony')
COMPILER=SDK/'ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc';DISASM=SDK/'toolchains/ark_disasm'
def specimen(name,profile,guard=True):
 c=catalog();op=lambda n,args=():encode(c[n],args)
 branch=lambda:Instruction(c['JEQZ_IMM8']['opcode'],target=Label(0),delta_width=1)
 pre=(op('LDNULL'),op('STRICTNOTEQ_IMM8_V8',(0,5)),branch(),op('LDUNDEFINED'),op('STRICTNOTEQ_IMM8_V8',(1,5)),branch()) if guard else ()
 body=pre+(op('LDUNDEFINED'),op('STA_V8',(0,)),op(name,(0,5,0)),op('RETURN'),Label(0),op('THROW_PATTERNNONCOERCIBLE_PREF_NONE'))
 return Module((op('DEFINEFUNC_IMM8_ID16_IMM8',(0,1,1)),op('STMODULEVAR_IMM8',(0,)),op('RETURNUNDEFINED')),profile=profile,functions=(FunctionCode('f',body,registers=2,arguments=4),),exports=('f',))
def dynamic_specimen(profile):
 c=catalog();op=lambda n,args=():encode(c[n],args)
 branch=lambda:Instruction(c['JEQZ_IMM8']['opcode'],target=Label(0),delta_width=1)
 body=(op('MOV_V8_V8',(0,11)),op('LDNULL'),op('STRICTNOTEQ_IMM8_V8',(0,0)),branch(),op('LDUNDEFINED'),op('STRICTNOTEQ_IMM8_V8',(1,0)),branch(),
       op('LDA_V8',(12,)),op('CALLARG0_IMM8',(2,)),op('STA_V8',(1,)),op('LDA_STR_ID16',(2,)),op('STA_V8',(2,)),op('LDA_V8',(1,)),op('ADD2_IMM8_V8',(3,2)),op('STA_V8',(3,)),
       op('LDA_V8',(3,)),op('LDOBJBYVALUE_IMM8_V8',(4,0)),op('STA_V8',(4,)),op('CREATEOBJECTWITHEXCLUDEDKEYS_IMM8_V8_V8',(0,0,3)),op('STA_V8',(5,)),
       op('CREATEEMPTYARRAY_IMM8',(6,)),op('STA_V8',(6,)),op('LDA_V8',(4,)),op('STOWNBYINDEX_IMM8_V8_IMM16',(7,6,0)),op('LDA_V8',(5,)),op('STOWNBYINDEX_IMM8_V8_IMM16',(9,6,1)),op('LDA_V8',(6,)),op('RETURN'),Label(0),op('THROW_PATTERNNONCOERCIBLE_PREF_NONE'))
 return Module((op('DEFINEFUNC_IMM8_ID16_IMM8',(0,1,2)),op('STMODULEVAR_IMM8',(0,)),op('RETURNUNDEFINED')),profile=profile,pool_strings=('',),functions=(FunctionCode('dynamic',body,registers=8,arguments=5),),exports=('dynamic',))
def probe(path):
 return [r for x in subprocess.check_output([BUILD/'isa_coverage_probe',path],text=True,stderr=subprocess.DEVNULL).splitlines() if (r:=json.loads(x))['type']=='instruction']
def generate():
 entries=[]
 for source_name,record_name in [('rest.js','isaRest'),('wide.js','isaRestWide')]:
  for mode,flags in [('default',[]),('o0',['--opt-level=0'])]:
   path=FIX/(source_name[:-3]+'-'+mode+'.abc');cmd=[str(COMPILER),'--module','--merge-abc','--extension=js','--record-name='+record_name,'--source-file='+source_name,*flags,'--output='+str(path.relative_to(ROOT)),str((FIX/source_name).relative_to(ROOT))];subprocess.run(cmd,check=True)
   entries.append(dict(path=path.name,sha256=sha(path),source=source_name,source_sha256=sha(FIX/source_name),origin='es2abc',command=cmd,opcodes=sorted({r['encoding']['opcode'] for r in probe(path)})))
 (FIX/'constructed.js').write_text('export function f(source){const {...rest}=source;return rest;}\n')
 for profile in [Profile.DYNAMIC1206,Profile.DYNAMIC2400]:
  for name in ['CREATEOBJECTWITHEXCLUDEDKEYS_IMM8_V8_V8','WIDE_CREATEOBJECTWITHEXCLUDEDKEYS_PREF_IMM16_V8_V8']:
   path=FIX/(name.lower()+'-'+profile.name.lower()+'.abc');path.write_bytes(build(specimen(name,profile))[0]);entries.append(dict(path=path.name,sha256=sha(path),source='constructed.js',source_sha256=sha(FIX/'constructed.js'),origin='constructed',profile=profile.name,kind=name,opcodes=[catalog()[name]['opcode']]))
 (FIX/'dynamic.js').write_text('export function dynamic(o,hook){if(o===null||o===undefined)throw({}=null);const key=""+hook();const {[key]:value,...rest}=o;return [value,rest];}\n')
 for profile in [Profile.DYNAMIC1206,Profile.DYNAMIC2400]:
  path=FIX/('dynamic-'+profile.name.lower()+'.abc');path.write_bytes(build(dynamic_specimen(profile))[0]);entries.append(dict(path=path.name,sha256=sha(path),source='dynamic.js',source_sha256=sha(FIX/'dynamic.js'),origin='constructed',profile=profile.name,kind='dynamic',opcodes=[catalog()['CREATEOBJECTWITHEXCLUDEDKEYS_IMM8_V8_V8']['opcode']]))
 (FIX/'manifest.json').write_text(json.dumps(dict(compiler_sha256=sha(COMPILER),disassembler_sha256=sha(DISASM),fixtures=entries),indent=2)+'\n')
 p=ROOT/'tests/isa/coverage_manifest.json';d=json.loads(p.read_text());prefix='tests/fixtures/isa/object_rest/';d['fixtures']=[e for e in d['fixtures'] if not e['path'].startswith(prefix)]
 for e in entries:d['fixtures'].append(dict(path=prefix+e['path'],sha256=e['sha256'],opcodes=e['opcodes'],classification='Unmeasured',oracles={},note='V7 object-rest Set and Proxy-unwrap differences; promotion requires occurrence evidence.'))
 d['oracle_producers']=[e for e in d['oracle_producers'] if e['test']!='isa_object_rest_source_test'];d['oracle_producers'].append(dict(test='isa_object_rest_source_test',producer='tests/isa/object_rest_test.py'));p.write_text(json.dumps(d,indent=2)+'\n')
class ObjectRest(unittest.TestCase):
 def test_complete_oracles(self):
  registry=json.loads((FIX/'manifest.json').read_text());self.assertEqual(sha(COMPILER),registry['compiler_sha256']);self.assertEqual(sha(DISASM),registry['disassembler_sha256'])
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,e in enumerate(registry['fixtures']):
    with self.subTest(fixture=e['path']):
     abc=FIX/e['path'];source=FIX/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
     if e['origin']=='constructed':self.assertEqual(build(dynamic_specimen(Profile[e['profile']]) if e['kind']=='dynamic' else specimen(e['kind'],Profile[e['profile']]))[0],abc.read_bytes())
     else:
      command=[('--output='+str(root/'compiled.abc')) if a.startswith('--output=') else a for a in e['command']];subprocess.run(command,check=True,cwd=ROOT);self.assertEqual(sha(root/'compiled.abc'),e['sha256'])
     subprocess.run([DISASM,abc,root/'decoded.pa'],check=True,capture_output=True)
     hits=[r for r in probe(abc) if r['encoding']['opcode'] in e['opcodes']];self.assertTrue(hits);self.assertTrue(all(r['lift']['status']=='ok' for r in hits))
     out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True);record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2);self.assertEqual(record['source_status'],0,record)
     emitted=out/record['source'];self.assertIn('...',emitted.read_text());subprocess.run(['node','--check',emitted],check=True,capture_output=True)
     result=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/isa_object_rest.mjs',source,emitted],text=True))
     if e.get('kind')=='dynamic':
      trip=root/(str(i)+'-trip');trip.mkdir();round_trip(trip,abc,out,emitted,BUILD/'abc_structure',BUILD/'isa_coverage_probe');self.assertTrue((trip/'recompiled.abc').exists());recert=root/(str(i)+'-recert');subprocess.run([BUILD/'abc_structure',trip/'recompiled.abc',recert,'--value-source','--language-source'],check=True,capture_output=True);again=next(r for r in json.loads((recert/'manifest.json').read_text())['records'] if r.get('source_status')!=2);self.assertEqual(again['source_status'],0,again);subprocess.run(['node',ROOT/'tests/integration/isa_object_rest.mjs',source,recert/again['source']],check=True,capture_output=True);default_abc=trip/'default.abc';subprocess.run([COMPILER,'--module','--merge-abc','--extension=js','--record-name=isaOracle','--source-file=input.js','--output='+str(default_abc),trip/'input.js'],check=True,capture_output=True);default_recert=root/(str(i)+'-default-recert');subprocess.run([BUILD/'abc_structure',default_abc,default_recert,'--value-source','--language-source'],check=True,capture_output=True);default_record=next(r for r in json.loads((default_recert/'manifest.json').read_text())['records'] if r.get('source_status')!=2);self.assertEqual(default_record['source_status'],0,default_record);subprocess.run(['node',ROOT/'tests/integration/isa_object_rest.mjs',source,default_recert/default_record['source']],check=True,capture_output=True)
     record_node(abc,out,source,__file__,result['cases'],BUILD/'abc_structure',exclusions=['V7 rest uses Set instead of CreateDataProperty and unwraps Proxy sources before copying. Node compares the two native sources; these two Ark observations are excluded.'])
 def test_scale_source(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for count in [200,201,400,800]:
    keys=[f'k{i}' for i in range(count)];source=root/f'scale{count}.js';source.write_text('export function wide(o){const {'+','.join(keys)+',...rest}=o;return [rest,'+','.join(keys)+'];}\n');abc=root/f'{count}.abc';subprocess.run([COMPILER,'--module','--merge-abc',source,'--output='+str(abc)],check=True,capture_output=True)
    out=root/str(count);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True);record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2);self.assertEqual(record['source_status'],0,record)
    subprocess.run(['node','--check',out/record['source']],check=True,capture_output=True);subprocess.run(['node',ROOT/'tests/integration/isa_object_rest.mjs',source,out/record['source']],check=True,capture_output=True)
 def test_refusals(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,source in enumerate(['export function f(o,k){const {[k]:v,...rest}=o;return [v,rest];}','export function f(o,k){const {[k()]:v,...rest}=o;return [v,rest];}']):
    js=root/f'{i}.js';js.write_text(source);abc=root/f'{i}.abc';subprocess.run([COMPILER,'--module','--merge-abc',js,'--output='+str(abc)],check=True,capture_output=True)
    out=root/f'bad{i}';subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True);record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2);self.assertNotEqual(record['source_status'],0);self.assertTrue(record.get('source_failure') or record.get('source_causes'))
   for profile in [Profile.DYNAMIC1206,Profile.DYNAMIC2400]:
    abc=root/('unguarded-'+profile.name+'.abc');abc.write_bytes(build(specimen('CREATEOBJECTWITHEXCLUDEDKEYS_IMM8_V8_V8',profile,False))[0]);out=root/('unguarded-'+profile.name);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True);record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2);self.assertNotEqual(record['source_status'],0)
if __name__=='__main__':
 if '--generate' in sys.argv:generate()
 else:unittest.main()
