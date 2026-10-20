// Test-only lexer shadow. Never erase field types from es2abc input: Sendable
// field masks are runtime metadata. Both sides must retain identical annotations.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const ts=require('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript/lib/typescript.js');
export function fieldTypeShadow(source,typed=false) {
 if(!typed)return {source,types:[]};
 const acorn=require('internal/deps/acorn/acorn/dist/acorn');
 const tokens=[...acorn.tokenizer(source,{ecmaVersion:'latest',sourceType:'module'})];
 const lazy=[];for(let i=1;i<tokens.length;++i)
   if(tokens[i-1].type.label==='import'&&tokens[i].type.label==='name'&&tokens[i].value==='lazy')lazy.push(tokens[i]);
 let grammar=source;
 for(const t of [...lazy].reverse())grammar=grammar.slice(0,t.start)+' '.repeat(t.end-t.start)+grammar.slice(t.end);
 const metadata=lazy.map(()=>['lazy-import','lazy']);
 // Lazy imports are Ark syntax. The JS shadow removes only their lexed
 // modifier, preserving offsets/metadata; es2abc ALWAYS receives the original.
 try{acorn.parse(grammar,{ecmaVersion:'latest',sourceType:'module'});return {source:grammar,types:metadata};}catch{}
 const file=ts.createSourceFile('input.ts',grammar,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
 assert.equal(file.parseDiagnostics.length,0,'typed lexer shadow parse');
 const work=[file],edits=[],types=[...metadata];
 while(work.length){const n=work.pop();ts.forEachChild(n,c=>{work.push(c);});
  if(!ts.isPropertyDeclaration(n)||!n.type)continue;
  const start=n.name.end,end=n.type.getStart(file);
  const scanner=ts.createScanner(ts.ScriptTarget.Latest,true,ts.LanguageVariant.Standard,grammar.slice(start,end));
  assert.equal(scanner.scan(),ts.SyntaxKind.ColonToken,'field annotation colon');
  const colon=start+scanner.getTokenPos();assert.equal(scanner.scan(),ts.SyntaxKind.EndOfFileToken);
  types.push([n.name.getText(file),source.slice(colon,n.type.end)]);edits.push([colon,n.type.end]);
 }
 let shadow=grammar;for(const [a,b]of edits.sort((a,b)=>b[0]-a[0]))shadow=shadow.slice(0,a)+shadow.slice(a,b).replace(/[^\r\n]/g,' ')+shadow.slice(b);
 return {source:shadow,types};
}
export function applyTokenEdits(source,edits) {
 for(const [a,b,text]of [...edits].sort((a,b)=>b[0]-a[0]))source=source.slice(0,a)+text+source.slice(b);
 return source;
}
