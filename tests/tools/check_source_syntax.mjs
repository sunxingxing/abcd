// Syntax check for published decompiler output. Plain .mjs is checked as an
// ES module. ArkTS (.ets) keeps approved native forms that Node cannot parse
// (`import lazy`, typed Sendable fields), so it is checked after removing only
// those forms: `import lazy` -> `import`, then TypeScript type erasure.
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
let failed = 0;
for (const path of process.argv.slice(2)) {
  let text = readFileSync(path, 'utf8');
  try {
    if (path.endsWith('.ets')) text = stripTypeScriptTypes(text.replace(/^import lazy /gm, 'import '), {mode: 'strip'});
    new vm.SourceTextModule(text, {identifier: path});
  } catch (error) {
    ++failed; console.log(JSON.stringify({path, error: String(error.message).split('\n')[0]}));
  }
}
process.exitCode = failed ? 1 : 0;
