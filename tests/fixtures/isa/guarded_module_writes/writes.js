export function setX(v) { x = v; return x; }
export function bump(k) { count = count + k; return count; }
const log = [];
globalThis.__log = log;
try { setX(5); } catch (e) { log.push(e.constructor.name); }
export let x = 1;
let count = 0;
try { log.push(setX(7)); log.push(bump(2)); } catch (e) { log.push(String(e)); }
