import * as ns from "data:text/javascript,export let count = 0; export const value = 17; export function bump(n) { count += n; return count; } export default 23;";
export function read() { return ns.value; }
export function call(n) { return ns.bump(n); }
export function live() { return ns.count; }
export function whole() { return ns; }
export function keys() { return Object.keys(ns).join(","); }
