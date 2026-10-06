import {readFileSync} from 'node:fs';
import {verifyWidgetRecordBatch} from './widget_record_state.mjs';

// Test-only boundary: already generated source and the independent oracle.
// Never pass these strings to the ABC engine or use them as recovered bodies.
const {source, reference, phase, first, count} = JSON.parse(readFileSync(0, 'utf8'));
const result = await verifyWidgetRecordBatch(source, reference, {phase, first, count});
process.stdout.write(JSON.stringify(result));
