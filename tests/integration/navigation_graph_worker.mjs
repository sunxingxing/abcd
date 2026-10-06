import {readFileSync} from 'node:fs';
import {verifyNavigationBatch} from './navigation_graph_state.mjs';

const {sources, reference, request} = JSON.parse(readFileSync(0, 'utf8'));
process.stdout.write(JSON.stringify(await verifyNavigationBatch(sources, reference, request)));
