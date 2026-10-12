import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {classReference, parseJavaScript} from './class_reference.mjs';
import {verifyShoppingViewModels} from './shopping_viewmodel_state.mjs';

const [tool, root] = process.argv.slice(2), abc = join(root, 'modules.abc');
const index = JSON.parse(readFileSync(new URL('../corpus/abc-index.json', import.meta.url)));
const identity = index.entries.find(row => row.corpus_id === 'paired' && row.relative_path === 'Immersive/modules.abc');
const bytes = readFileSync(abc);assert.ok(identity);assert.equal(bytes.length, identity.size);
assert.equal(createHash('sha256').update(bytes).digest('hex'), identity.sha256);
const records = [
  [7395,'Breakpoint','commons/src/main/ets/utils/Breakpoint'],
  [16247,'HomeConstants','shopping/src/main/ets/constants/HomeConstants'],
  [16474,'HomeModel','shopping/src/main/ets/model/HomeModel'],
  [16752,'IconInfoModel','shopping/src/main/ets/model/IconInfoModel'],
  [17175,'RecommendModel','shopping/src/main/ets/model/RecommendModel'],
  [17544,'SectionModel','shopping/src/main/ets/model/SectionModel'],
  [28887,'HomeViewModel','shopping/src/main/ets/viewmodel/HomeViewModel'],
  [29119,'IconInfoViewModel','shopping/src/main/ets/viewmodel/IconInfoViewModel'],
  [29382,'RecommendViewModel','shopping/src/main/ets/viewmodel/RecommendViewModel'],
  [29619,'SectionViewModel','shopping/src/main/ets/viewmodel/SectionViewModel'],
];
const sources = {}, reference = {}, temporary = mkdtempSync(join(tmpdir(),'abcd-shopping-viewmodels-'));
try {
  const output = join(temporary,'output');
  execFileSync(tool,[abc,output,'--value-source','--language-numeric','--language-constructors'],{timeout:60_000});
  const manifest = JSON.parse(readFileSync(join(output,'manifest.json')));
  assert.equal(manifest.sha256,identity.sha256);assert.equal(manifest.source_route,'value_tree');
  assert.equal(manifest.source_contract,'language_numeric_constructors_v1');
  for(const [entity,name] of records) {
    const row = manifest.records.find(row=>row.entity===entity);assert.ok(row);
    assert.equal(row.source_complete,true);assert.equal(row.source_status,0);
    assert.equal(row.source_route,'value_tree');assert.equal(row.legacy_writer_used, undefined);
    sources[name]=readFileSync(join(output,row.source),'utf8');parseJavaScript(sources[name]);
    assert.doesNotMatch(sources[name],/INCOMPLETE|UNRECOVERED/);
  }
} finally {rmSync(temporary,{recursive:true});}
// Paired code is read only after the ABC-only process exits. The shared adapter
// erases type-only fields while preserving every initializer and method body.
for(const [,name,path] of records)reference[name]=classReference(readFileSync(join(root,'Immersive_src',path+'.ts'),'utf8')).source;
console.log(JSON.stringify(await verifyShoppingViewModels(sources,reference)));
