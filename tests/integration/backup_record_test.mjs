import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {stripTypeScriptTypes} from 'node:module';
import {verifyBackupRecords} from './backup_records.mjs';

const [tool,corpus] = process.argv.slice(2);
const index = JSON.parse(readFileSync(new URL('../corpus/abc-index.json',import.meta.url),'utf8'));
// Generate every complete record before reading any independent paired source.
const records = [
  ['Immersive',9308,9460,'Immersive_src/default/src/main/ets/entrybackupability/EntryBackupAbility.ts'],
  ['pasteboard',5246,5409,'pasteboard_src/src/main/ets/entrybackupability/EntryBackupAbility.ts'],
  ['push-kit-sample-code-clientdemo-arkts',6659,6809,'src/entry/src/main/ets/entrybackupability/EntryBackupAbility.ts'],
].map(([app,entity,method,paired]) => {
  const relative = `${app}/modules.abc`, abc = join(corpus,relative);
  const identity = index.entries.find(row => row.corpus_id === 'paired' && row.relative_path === relative);
  assert.ok(identity,relative);
  const bytes = readFileSync(abc);
  assert.equal(bytes.length,identity.size);
  assert.equal(createHash('sha256').update(bytes).digest('hex'),identity.sha256);
  const report = JSON.parse(execFileSync(tool,[abc,String(method),'--record-source','--value-source','--language-constructors'],
    {encoding:'utf8',timeout:60000,maxBuffer:8 * 1024 * 1024}));
  assert.equal(report.record,entity); assert.equal(report.route,'value_tree');
  assert.equal(report.source_complete,true); assert.equal(report.legacy_writer_used,false);
  assert.doesNotMatch(report.source,/INCOMPLETE|UNRECOVERED/);
  return {app,source:report.source,paired:join(corpus,app,paired)};
});
for (const row of records) row.reference = stripTypeScriptTypes(readFileSync(row.paired,'utf8'),{mode:'transform'});
console.log(JSON.stringify(await verifyBackupRecords(records)));
