// Test-only evidence transaction, invoked after the suite's Node assertions.
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {mkdirSync} from 'node:fs';
export function recordNodeEvidence({abc, output, original, producer, cases, engine, exclusions=[], records}) {
  if (!process.env.ABCD_ISA_ORACLE_DIR) return;
  mkdirSync(process.env.ABCD_ISA_ORACLE_DIR,{recursive:true});
  const script=fileURLToPath(new URL('./oracle_evidence.py',import.meta.url));
  const args=[script,'--abc',abc,'--export',output,'--original',original,
    '--producer',fileURLToPath(producer),'--cases',String(cases),'--engine',engine];
  for (const record of records || []) args.push('--record',String(record));
  for (const exclusion of exclusions) args.push('--exclusion',exclusion);
  const result=spawnSync('python3',args,{encoding:'utf8',timeout:120000,maxBuffer:4*1024*1024});
  if (result.error || result.status!==0) throw new Error(`ISA evidence failed: ${result.error||result.stderr}`);
}
