// Module entries with real top-level try/catch take the phased route.
const log = globalThis.__entryLog ?? (globalThis.__entryLog = []);
const store = {};
let count = 0;
export function get(key) { return store[key]; }
export function inc() { count++; return count; }
try {
  store.a = globalThis.__entryProbe("a");
} catch (e) {
  store.err = e;
  log.push("caught");
  log.push(e);
}
try {
  log.push("second", inc());
} catch {
}
export const ready = log.length;
export let late = get("a") ?? "none";
