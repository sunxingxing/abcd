import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const rows=execFileSync(process.argv[2],['--sources'],{encoding:'utf8'}).trim().split('\n').map(JSON.parse);
for(const {place,source} of rows){
 const run=Function('"use strict";return '+source)();
 let value=run(x=>x);
 if(place===6){assert.equal(value,'');continue;}
 if(place===7){assert.equal(value,7);continue;}
 if(place===2)value=value[0];
 if(place===3)value=value.key;
 assert.equal(value.name,place===3?'key':place===4?'chosen':'');
 assert.equal(await value(19),19);
 if(place===5)assert.equal(run(false),undefined);
}
console.log(`${rows.length} grouping family contexts: syntax, values and inferred names passed`);
