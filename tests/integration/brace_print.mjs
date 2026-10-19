import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
const source=execFileSync(process.argv[2],['emit'],{encoding:'utf8'});
assert(source.includes('else if'));assert.equal(Function('return '+source)()(),22);
// Empty-scope blocks preserve StatementList's completion value, including
// UpdateEmpty and abrupt completions. These are independent language oracles.
for(const [old,next] of [
 ['{ 1; { 2; } }','{ 1; 2; }'],
 ['{ 1; { if(false) 2; } }','{ 1; if(false) 2; }'],
 ['let x=0; { { x++; x; } }','let x=0; { x++; x; }'],
 ['(()=>{ { return 3; } })()','(()=>{ return 3; })()'],
 ['(()=>{try{{throw 7;}}catch(e){return e;}})()','(()=>{try{throw 7;}catch(e){return e;}})()'],
 ['if(false){}else{if(true){7;}else{8;}}','if(false){}else if(true){7;}else{8;}'],
 ['if(true){if(false){7;}}else{if(true){8;}}','if(true){if(false){7;}}else if(true){8;}']])assert.equal(vm.runInNewContext(old),vm.runInNewContext(next));
console.log('brace printing: engine tree and 7 completion/dangling-else oracles passed');
