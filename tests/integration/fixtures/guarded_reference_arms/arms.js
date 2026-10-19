import { receiver } from './dependency.mjs';
export function pick(flag) {
  return globalThis.receiver.method(flag.state ? left : right);
}
export function fromImport(flag) {
  return receiver.method(flag.state ? left : right);
}
export function asCallee(flag) {
  return (flag.state ? calleeA : calleeB)(globalThis.argument());
}
globalThis.probe(pick, fromImport, asCallee);
let left = globalThis.left;
const right = globalThis.right;
const calleeA = globalThis.calleeA;
const calleeB = globalThis.calleeB;
export function update(value) { left = value; }
