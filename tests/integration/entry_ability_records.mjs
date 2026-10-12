import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {stripTypeScriptTypes} from 'node:module';
import {verifyEntryAbilities} from './entry_ability_lifecycle.mjs';

const [tool,corpus] = process.argv.slice(2);
const index = JSON.parse(readFileSync(new URL('../corpus/abc-index.json',import.meta.url),'utf8'));
// Finish actual ABC-only generation before opening any independent paired TS.
const records = [
  ['pasteboard',4767,4917,'pasteboard_src/src/main/ets/entryability/EntryAbility.ts'],
  ['MultiCommunityApplication',40957,41080,'src/phone/src/main/ets/entryability/EntryAbility.ts'],
].map(([app,entity,method,paired]) => {
  const relative = `${app}/modules.abc`, abc = join(corpus,relative);
  const identity = index.entries.find(row => row.corpus_id === 'paired' && row.relative_path === relative);
  assert.ok(identity,relative);
  const bytes = readFileSync(abc);
  assert.equal(bytes.length,identity.size);
  assert.equal(createHash('sha256').update(bytes).digest('hex'),identity.sha256);
  const report = JSON.parse(execFileSync(tool,[abc,String(method),'--record-source','--value-source',
    '--language-numeric','--language-constructors'],{encoding:'utf8',timeout:60000,maxBuffer:8 * 1024 * 1024}));
  assert.equal(report.record,entity); assert.equal(report.route,'value_tree');
  assert.equal(report.source_complete,true); assert.equal(report.legacy_writer_used, undefined);
  assert.doesNotMatch(report.source,/INCOMPLETE|UNRECOVERED/);
  return {app,source:report.source,paired:join(corpus,app,paired)};
});
for (const row of records) row.reference = stripTypeScriptTypes(readFileSync(row.paired,'utf8'),{mode:'transform'});
console.log(JSON.stringify(await verifyEntryAbilities(records)));
