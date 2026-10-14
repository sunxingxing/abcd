import { state } from './values.js';
export function check(receiver) {
  const x = receiver.list[0];
  return x === state.A || x === state.B;
}
export function andCheck(receiver) {
  const x = receiver.list[0];
  return x !== state.A && x !== state.B;
}
