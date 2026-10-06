import {readFileSync} from 'node:fs';
import {checkContentRecord} from './content_record_state.mjs';

const {sources, reference, request} = JSON.parse(readFileSync(0, 'utf8'));
console.log(JSON.stringify(await checkContentRecord(sources, reference, request)));
