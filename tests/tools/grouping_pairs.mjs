import {readFileSync,writeFileSync} from 'node:fs';
import assert from 'node:assert/strict';
import {fieldTypeShadow,applyTokenEdits} from './grouping_types.mjs';
import {createRequire} from 'node:module';
import {inferGroupingRemovals,checkValueCallRemovals,checkBraceRemovals,checkNegatedEqualityRemovals,rewriteNegatedEqualities,rewriteReadableStatements} from './grouping_tokens.mjs';
const require=createRequire(import.meta.url),acorn=require('internal/deps/acorn/acorn/dist/acorn');
const pairs=JSON.parse(readFileSync(process.argv[2],'utf8')),results=[];
for(const pair of pairs){
  const old=readFileSync(pair.old,'utf8'),next=readFileSync(pair.new,'utf8');
  const typed=pair.old.endsWith('.ets');
  const aShadow=fieldTypeShadow(old,typed),bShadow=fieldTypeShadow(next,typed);
  assert.deepEqual(aShadow.types,bShadow.types,'field type metadata changed');
  const result={id:pair.id,removed:0,oldDouble:(old.match(/\(\(/g)||[]).length,newDouble:(next.match(/\(\(/g)||[]).length,oldObject:(old.match(/\(\{/g)||[]).length,newObject:(next.match(/\(\{/g)||[]).length};
  try{if(process.argv.includes('--readable-statements')){const proof=rewriteReadableStatements(aShadow.source,bShadow.source);result.removed=proof.removed;result.readable=proof.counts;}else if(process.argv.includes('--negated-equality')){const proof=checkNegatedEqualityRemovals(aShadow.source,bShadow.source);result.removed=proof.removed;result.negated=proof.negated;}else result.removed=process.argv.includes('--braces')?checkBraceRemovals(old,next,result):process.argv.includes('--value-calls')?checkValueCallRemovals(old,next):inferGroupingRemovals(aShadow.source,bShadow.source).length;result.status='pass';}
  catch(e){result.status=pair.old.endsWith('.ets')?'unverified':'fail';result.reason=String(e);}
  // Apply only identifier-valued string export aliases. No text matching of
  // strings/comments; both sides receive the same syntax adaptation.
  function adapt(source){
    const ast=acorn.parse(fieldTypeShadow(source,typed).source,{ecmaVersion:'latest',sourceType:'module'}),edits=[];
    for(const node of ast.body)if(node.type==='ExportNamedDeclaration')for(const spec of node.specifiers){
      if(spec.exported.type==='Literal' && typeof spec.exported.value==='string'){
        const name=spec.exported.value;
        try{const parsed=acorn.parse(`const x=0;export {x as ${name}};`,{ecmaVersion:'latest',sourceType:'module'});if(parsed.body[1].specifiers[0].exported.type==='Identifier')edits.push([spec.exported.start,spec.exported.end,name]);}catch{}
      }
    }
    for(const [a,b,text] of edits.sort((a,b)=>b[0]-a[0]))source=source.slice(0,a)+text+source.slice(b);
    return {source,count:edits.length};
  }
  try{const a=adapt(old),b=adapt(next);if(a.count!==b.count)throw Error('adaptation count differs');writeFileSync(pair.adapted_old,a.source);writeFileSync(pair.adapted_new,b.source);result.exportAdaptations=a.count;if(pair.adapted_readable){const proof=rewriteReadableStatements(fieldTypeShadow(a.source,typed).source,fieldTypeShadow(b.source,typed).source);writeFileSync(pair.adapted_readable,applyTokenEdits(a.source,proof.edits));}if(pair.adapted_negated){const proof=rewriteNegatedEqualities(fieldTypeShadow(a.source,typed).source,fieldTypeShadow(b.source,typed).source);writeFileSync(pair.adapted_negated,applyTokenEdits(a.source,proof.edits));}if(pair.adapted_node){writeFileSync(pair.adapted_node,bShadow.source);result.typeAnnotations=aShadow.types;}}
  catch(e){writeFileSync(pair.adapted_old,old);writeFileSync(pair.adapted_new,next);result.adaptationReason=String(e);}
  results.push(result);
}
writeFileSync(process.argv[3],JSON.stringify(results,null,2));
