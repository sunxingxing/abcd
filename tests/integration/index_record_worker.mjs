import {readFileSync} from 'node:fs';
import {verifyIndexRecordBatch} from './index_record_state.mjs';

// Already generated ESM and the independent paired oracle are test-only input.
const {source, routes, reference, referenceRoutes, request} = JSON.parse(readFileSync(0, 'utf8'));
process.stdout.write(JSON.stringify(await verifyIndexRecordBatch(source, routes, reference, referenceRoutes, request)));
