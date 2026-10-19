export function choose(input) {
  return input.flag || input.test({id: 1, params: [], tail: input.value}) ? 11 : 22;
}
export function conjunction(input) {
  return input.flag && input.test({id: 1, params: [], tail: input.value}) ? 11 : 22;
}
export function branch(input) {
  if (input.flag || input.test({id: 1, params: [], tail: input.value})) {
    return input.yes;
  }
  return input.no;
}
export function nested(input) {
  return input.flag || input.test({id: 1, params: [], tail: {value: input.value}}) ? 11 : 22;
}
