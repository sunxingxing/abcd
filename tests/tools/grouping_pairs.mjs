import {readFileSync,writeFileSync} from 'node:fs';
import {createRequire} from 'node:module';
import {inferGroupingRemovals,checkValueCallRemovals} from './grouping_tokens.mjs';
const require=createRequire(import.meta.url),acorn=require('internal/deps/acorn/acorn/dist/acorn');
const pairs=JSON.parse(readFileSync(process.argv[2],'utf8')),results=[];
for(const pair of pairs){
  const old=readFileSync(pair.old,'utf8'),next=readFileSync(pair.new,'utf8');
  const result={id:pair.id,removed:0,oldDouble:(old.match(/\(\(/g)||[]).length,newDouble:(next.match(/\(\(/g)||[]).length,oldObject:(old.match(/\(\{/g)||[]).length,newObject:(next.match(/\(\{/g)||[]).length};
  try{result.removed=process.argv.includes('--value-calls')?checkValueCallRemovals(old,next):inferGroupingRemovals(old,next).length;result.status='pass';}
  catch(e){result.status=pair.old.endsWith('.ets')?'unverified':'fail';result.reason=String(e);}
  // Apply only identifier-valued string export aliases. No text matching of
  // strings/comments; both sides receive the same syntax adaptation.
  function adapt(source){
    const ast=acorn.parse(source,{ecmaVersion:'latest',sourceType:'module'}),edits=[];
    for(const node of ast.body)if(node.type==='ExportNamedDeclaration')for(const spec of node.specifiers){
      if(spec.exported.type==='Literal' && typeof spec.exported.value==='string'){
        const name=spec.exported.value;
        try{const parsed=acorn.parse(`const x=0;export {x as ${name}};`,{ecmaVersion:'latest',sourceType:'module'});if(parsed.body[1].specifiers[0].exported.type==='Identifier')edits.push([spec.exported.start,spec.exported.end,name]);}catch{}
      }
    }
    for(const [a,b,text] of edits.sort((a,b)=>b[0]-a[0]))source=source.slice(0,a)+text+source.slice(b);
    return {source,count:edits.length};
  }
  try{const a=adapt(old),b=adapt(next);if(a.count!==b.count)throw Error('adaptation count differs');writeFileSync(pair.adapted_old,a.source);writeFileSync(pair.adapted_new,b.source);result.exportAdaptations=a.count;}
  catch(e){writeFileSync(pair.adapted_old,old);writeFileSync(pair.adapted_new,next);result.adaptationReason=String(e);}
  results.push(result);
}
writeFileSync(process.argv[3],JSON.stringify(results,null,2));
