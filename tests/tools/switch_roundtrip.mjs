// Symmetric, test-only per-method wrappers for grouping_bytecode.py. Preserve
// every body/signature token; give both methods the same record/export identity.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {join} from 'node:path';
const require=createRequire(import.meta.url),acorn=require('internal/deps/acorn/acorn/dist/acorn');
const [original,emitted,out]=process.argv.slice(2);
function methods(file){const text=readFileSync(file,'utf8'),ast=acorn.parse(text,{ecmaVersion:'latest',sourceType:'module'}),result=new Map();
  for(const outer of ast.body){const node=outer.type==='ExportNamedDeclaration'?outer.declaration:outer;
    if(node?.type==='FunctionDeclaration')result.set(node.id.name,{parameters:node.params.map(p=>text.slice(p.start,p.end)).join(', '),body:text.slice(node.body.start,node.body.end)});
    if(node?.type==='VariableDeclaration')for(const d of node.declarations)if(d.id.type==='Identifier'&&d.init?.type==='FunctionExpression')result.set(d.id.name,{parameters:d.init.params.map(p=>text.slice(p.start,p.end)).join(', '),body:text.slice(d.init.body.start,d.init.body.end)});
  }return result;
}
const old=methods(original),fresh=methods(emitted);assert.deepEqual([...old.keys()].sort(),[...fresh.keys()].sort());
const index=[];let id=0;
for(const [name,a] of old){const b=fresh.get(name);const number=++id;index.push({entity:number,name});
  for(const [side,m] of [['old',a],['new',b]]){const root=join(out,side,'m'+number);mkdirSync(root,{recursive:true});
    writeFileSync(join(root,'method.mjs'),`let ${name} = function(${m.parameters}) ${m.body};\nexport { ${name} as "${name}" };\n`);
    writeFileSync(join(root,'manifest.json'),JSON.stringify({records:[{entity:number,source_status:0,source:'method.mjs'}]})+'\n');
  }
}
writeFileSync(join(out,'methods.json'),JSON.stringify(index,null,2)+'\n');
