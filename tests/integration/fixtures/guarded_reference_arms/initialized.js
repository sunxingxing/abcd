import { receiver } from './dependency.mjs';
let left = globalThis.left;
const right = globalThis.right;
const calleeA = globalThis.calleeA;
const calleeB = globalThis.calleeB;
export const pick = function(flag) {
  return globalThis.receiver.method(flag.state ? left : right);
};
export const fromImport = function(flag) {
  return receiver.method(flag.state ? left : right);
};
export const asCallee = function(flag) {
  return (flag.state ? calleeA : calleeB)(globalThis.argument());
};
export const update = function(value) { left = value; };
globalThis.probe(pick, fromImport, asCallee);
