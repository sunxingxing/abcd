import lazy { value, bump } from './dep.mjs';
export function read() { return value; }
export function change() { bump(); return value; }
export function caught() { let result; try { result = value; } catch (e) { result = e; } return result; }

globalThis.lazyEvents.push('main');
