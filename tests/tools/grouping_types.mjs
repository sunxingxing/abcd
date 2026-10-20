// Test-only lexer shadow. Never erase field types from es2abc input: Sendable
// field masks are runtime metadata. Both sides must retain identical annotations.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const ts=require('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript/lib/typescript.js');
export function fieldTypeShadow(source,typed=false) {
 if(!typed)return {source,types:[]};
 const file=ts.createSourceFile('input.ts',source,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
 assert.equal(file.parseDiagnostics.length,0,'typed lexer shadow parse');
 const work=[file],edits=[],types=[];
 while(work.length){const n=work.pop();ts.forEachChild(n,c=>{work.push(c);});
  if(!ts.isPropertyDeclaration(n)||!n.type)continue;
  const start=n.name.end,end=n.type.getStart(file);
  const scanner=ts.createScanner(ts.ScriptTarget.Latest,true,ts.LanguageVariant.Standard,source.slice(start,end));
  assert.equal(scanner.scan(),ts.SyntaxKind.ColonToken,'field annotation colon');
  const colon=start+scanner.getTokenPos();assert.equal(scanner.scan(),ts.SyntaxKind.EndOfFileToken);
  types.push([n.name.getText(file),source.slice(colon,n.type.end)]);edits.push([colon,n.type.end]);
 }
 let shadow=source;for(const [a,b]of edits.sort((a,b)=>b[0]-a[0]))shadow=shadow.slice(0,a)+shadow.slice(a,b).replace(/[^\r\n]/g,' ')+shadow.slice(b);
 return {source:shadow,types};
}
export function applyTokenEdits(source,edits) {
 for(const [a,b,text]of [...edits].sort((a,b)=>b[0]-a[0]))source=source.slice(0,a)+text+source.slice(b);
 return source;
}
