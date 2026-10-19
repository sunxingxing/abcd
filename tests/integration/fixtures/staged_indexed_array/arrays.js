export function strings(sink, input) {
  return sink.f({before: input.before, value: [input.mode === 1 ? 'first' : input.mode === 2 ? 'second' : 'third'], after: input.after});
}
export function values(sink, input) {
  return sink.f({value: [input.flag ? input[input.key] : input.other], after: input.after});
}
export function several(sink, input) {
  return sink.f({value: [input.flag ? 'a' : 'b', input.second ? input.left() : input.right()], after: input.after});
}
